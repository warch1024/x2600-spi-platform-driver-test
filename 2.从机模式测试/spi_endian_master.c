#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pattern.h"

#define DEFAULT_DEVICE "/dev/spidev1.0"
#define DEFAULT_SPEED_HZ 1000000U
#define DEFAULT_LOOPS 1U
#define DEFAULT_WORDS 8U
#define DEFAULT_START_DELAY_MS 0U
#define DEFAULT_CASE_DELAY_MS 100U
#define MAX_WORDS 4096U
#define MAX_SPEED_HZ 40000000U

typedef enum {
    TEST_RX = 0,
    TEST_TX,
    TEST_DUPLEX,
} test_direction_t;

static int parse_uint(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno || !*text || *end || parsed > UINT32_MAX)
        return -1;
    *value = (unsigned int)parsed;
    return 0;
}

static void usage(const char *name)
{
    fprintf(stderr,
            "Usage: %s [--device PATH] [--speed HZ] [--loops N] "
            "[--bits 8|16|32] [--words N] [--direction rx|tx|duplex] "
            "[--start-delay-ms N] [--case-delay-ms N]\n",
            name);
}

static int valid_bits(unsigned int bits)
{
    return bits == 8U || bits == 16U || bits == 32U;
}

static int valid_words(unsigned int words)
{
    return words > 0U && words <= MAX_WORDS;
}

static int parse_direction(const char *text, test_direction_t *direction)
{
    if (!strcmp(text, "rx"))
        *direction = TEST_RX;
    else if (!strcmp(text, "tx"))
        *direction = TEST_TX;
    else if (!strcmp(text, "duplex"))
        *direction = TEST_DUPLEX;
    else
        return -1;
    return 0;
}

static const char *direction_name(test_direction_t direction)
{
    switch (direction) {
    case TEST_RX:
        return "rx";
    case TEST_TX:
        return "tx";
    case TEST_DUPLEX:
        return "duplex";
    default:
        return "unknown";
    }
}

static int wait_ms(unsigned int delay_ms)
{
    struct timeval timeout;

    timeout.tv_sec = delay_ms / 1000U;
    timeout.tv_usec = (delay_ms % 1000U) * 1000U;
    if (select(0, NULL, NULL, NULL, &timeout) < 0) {
        if (errno == EINTR)
            return wait_ms(delay_ms);
        perror("delay");
        return -1;
    }
    return 0;
}

static void print_bytes(const uint8_t *data, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
        printf("%02x%s", data[i], i + 1U == length ? "" : " ");
    putchar('\n');
}

static int configure_spi(int fd, unsigned int bits, unsigned int speed_hz)
{
    uint8_t mode = SPI_MODE_3;
    uint8_t lsb_first = 0;
    uint8_t bits_per_word = (uint8_t)bits;
    uint32_t speed = speed_hz;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("configure spidev");
        return -1;
    }
    return 0;
}

static int send_case(int fd,
                     unsigned int bits,
                     unsigned int speed_hz,
                     unsigned int words,
                     unsigned int sequence,
                     test_direction_t direction)
{
    struct spi_ioc_transfer transfer;
    uint8_t *tx;
    uint8_t *rx = NULL;
    uint8_t *expected = NULL;
    size_t length;
    et_result_t result;
    int ret;

    length = et_word_bytes(bits) * (size_t)words;
    tx = malloc(length);
    if (!tx)
        return -1;
    if (et_build_pattern_sequence(bits, tx, words, sequence, ET_STREAM_MASTER) != length) {
        free(tx);
        return -1;
    }

    if (direction != TEST_RX) {
        rx = calloc(1, length);
        expected = malloc(length);
        if (!rx || !expected ||
            et_build_pattern_sequence(bits, expected, words, sequence, ET_STREAM_SLAVE) != length) {
            free(expected);
            free(rx);
            free(tx);
            return -1;
        }
    }

    if (configure_spi(fd, bits, speed_hz) < 0) {
        free(expected);
        free(rx);
        free(tx);
        return -1;
    }

    memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = (unsigned long)tx;
    transfer.rx_buf = (unsigned long)rx;
    transfer.len = (uint32_t)length;
    transfer.speed_hz = speed_hz;
    transfer.bits_per_word = (uint8_t)bits;
    transfer.cs_change = 1;

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &transfer);
    if (ret < 0) {
        perror("SPI_IOC_MESSAGE");
        free(expected);
        free(rx);
        free(tx);
        return -1;
    }
    if (ret != (int)length) {
        fprintf(stderr, "[master] case=%u bits=%u transferred=%d expected=%zu\n",
                sequence, bits, ret, length);
        free(expected);
        free(rx);
        free(tx);
        return -1;
    }

    printf("[master] case=%u direction=%s bits=%u words=%u bytes=%zu sent; cs released",
           sequence, direction_name(direction), bits, words, length);
    if (direction != TEST_RX) {
        result = et_compare(bits, expected, rx, words);
        printf(" miso=%s\n", et_result_name(result));
        if (result != ET_MATCH) {
            printf("[master] expected miso: ");
            print_bytes(expected, length);
            printf("[master] actual miso:   ");
            print_bytes(rx, length);
        }
    } else {
        putchar('\n');
    }

    free(expected);
    free(rx);
    free(tx);
    return direction == TEST_RX || result == ET_MATCH ? 0 : -1;
}

static int has_next_case(unsigned int loop,
                         unsigned int loops,
                         size_t width_index,
                         int selected_bits)
{
    return loop + 1U < loops || (!selected_bits && width_index < 2U);
}

int main(int argc, char **argv)
{
    static const unsigned int widths[] = { 8U, 16U, 32U };
    const char *device = DEFAULT_DEVICE;
    unsigned int speed_hz = DEFAULT_SPEED_HZ;
    unsigned int loops = DEFAULT_LOOPS;
    unsigned int words = DEFAULT_WORDS;
    unsigned int selected_bits = 0U;
    unsigned int start_delay_ms = DEFAULT_START_DELAY_MS;
    unsigned int case_delay_ms = DEFAULT_CASE_DELAY_MS;
    test_direction_t direction = TEST_RX;
    unsigned int loop;
    unsigned int sequence = 0U;
    size_t i;
    int fd;

    for (i = 1; i < (size_t)argc; i++) {
        if (!strcmp(argv[i], "--device") && ++i < (size_t)argc) {
            device = argv[i];
        } else if (!strcmp(argv[i], "--speed") && ++i < (size_t)argc) {
            if (parse_uint(argv[i], &speed_hz) < 0 || !speed_hz || speed_hz > MAX_SPEED_HZ)
                return usage(argv[0]), 1;
        } else if (!strcmp(argv[i], "--loops") && ++i < (size_t)argc) {
            if (parse_uint(argv[i], &loops) < 0 || !loops)
                return usage(argv[0]), 1;
        } else if (!strcmp(argv[i], "--bits") && ++i < (size_t)argc) {
            if (parse_uint(argv[i], &selected_bits) < 0 || !valid_bits(selected_bits))
                return usage(argv[0]), 1;
        } else if (!strcmp(argv[i], "--words") && ++i < (size_t)argc) {
            if (parse_uint(argv[i], &words) < 0 || !valid_words(words))
                return usage(argv[0]), 1;
        } else if (!strcmp(argv[i], "--direction") && ++i < (size_t)argc) {
            if (parse_direction(argv[i], &direction) < 0)
                return usage(argv[0]), 1;
        } else if (!strcmp(argv[i], "--start-delay-ms") && ++i < (size_t)argc) {
            if (parse_uint(argv[i], &start_delay_ms) < 0)
                return usage(argv[0]), 1;
        } else if (!strcmp(argv[i], "--case-delay-ms") && ++i < (size_t)argc) {
            if (parse_uint(argv[i], &case_delay_ms) < 0)
                return usage(argv[0]), 1;
        } else {
            return usage(argv[0]), 1;
        }
    }

    if (direction != TEST_RX && words > 64U) {
        fprintf(stderr, "[master] tx/duplex currently supports at most 64 words\n");
        return 1;
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror(device);
        return 1;
    }

    if (selected_bits)
        printf("[master] device=%s mode=3 speed=%uHz loops=%u bits=%u words=%u "
               "direction=%s start_delay=%ums case_delay=%ums\n",
               device, speed_hz, loops, selected_bits, words,
               direction_name(direction), start_delay_ms, case_delay_ms);
    else
        printf("[master] device=%s mode=3 speed=%uHz loops=%u bits=all words=%u "
               "direction=%s start_delay=%ums case_delay=%ums\n",
               device, speed_hz, loops, words, direction_name(direction),
               start_delay_ms, case_delay_ms);
    if (start_delay_ms && wait_ms(start_delay_ms) < 0) {
        close(fd);
        return 1;
    }

    for (loop = 0; loop < loops; loop++) {
        for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
            if (selected_bits && widths[i] != selected_bits)
                continue;
            if (send_case(fd, widths[i], speed_hz, words, sequence, direction) < 0) {
                close(fd);
                return 1;
            }
            sequence++;
            if (case_delay_ms && has_next_case(loop, loops, i, selected_bits) &&
                wait_ms(case_delay_ms) < 0) {
                close(fd);
                return 1;
            }
        }
    }

    close(fd);
    return 0;
}
