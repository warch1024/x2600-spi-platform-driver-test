#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <time.h>
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
#define SUITE_CASE_DELAY_MS 200U

typedef enum {
    TEST_RX = 0,
    TEST_TX,
    TEST_DUPLEX,
} test_direction_t;

static FILE *suite_report;
static unsigned int suite_passed;
static unsigned int suite_failed;
static unsigned int suite_case_delay_ms = SUITE_CASE_DELAY_MS;
static char suite_last_error[160];

static void report_printf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (suite_report) {
        va_start(args, format);
        vfprintf(suite_report, format, args);
        va_end(args);
        fflush(suite_report);
    }
}

static void suite_result(const char *name, int passed, const char *detail)
{
    report_printf("[suite] %-16s %s: %s\n", name, passed ? "PASS" : "FAIL", detail);
    if (suite_report)
        fprintf(suite_report, "- **%s**: **%s** - %s\n", name, passed ? "PASS" : "FAIL", detail);
    if (passed)
        suite_passed++;
    else
        suite_failed++;
}

static void suite_set_error(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(suite_last_error, sizeof(suite_last_error), format, args);
    va_end(args);
}

static FILE *open_suite_report(const char *argv0, const char *path)
{
    char default_path[256];
    char timestamp[32];
    const char *slash;
    time_t now;
    struct tm *time_info;

    if (!path) {
        slash = strrchr(argv0, '/');
        now = time(NULL);
        time_info = localtime(&now);
        if (!time_info || !strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", time_info))
            return NULL;
        if (slash)
            snprintf(default_path, sizeof(default_path), "%.*s/spi_endian_master_suite_%s.md",
                     (int)(slash - argv0), argv0, timestamp);
        else
            snprintf(default_path, sizeof(default_path), "./spi_endian_master_suite_%s.md", timestamp);
        path = default_path;
    }

    suite_report = fopen(path, "w");
    if (!suite_report) {
        perror(path);
        return NULL;
    }
    fprintf(suite_report, "# SPI SSLV Master Suite Report\n\n");
    fprintf(suite_report, "## Results\n\n");
    report_printf("[suite] report=%s\n", path);
    return suite_report;
}

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
            "[--start-delay-ms N] [--case-delay-ms N] [--suite] [--log PATH]\n",
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
    if (suite_report) {
        for (i = 0; i < length; i++)
            fprintf(suite_report, "%02x%s", data[i], i + 1U == length ? "" : " ");
        fputc('\n', suite_report);
    }
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
                     test_direction_t direction,
                     int append_zero)
{
    struct spi_ioc_transfer transfer;
    uint8_t *tx;
    uint8_t *rx = NULL;
    uint8_t *expected = NULL;
    size_t word_bytes;
    unsigned int wire_words;
    size_t payload_length;
    size_t length;
    et_result_t result = ET_MISMATCH;
    int ret;

    word_bytes = et_word_bytes(bits);
    wire_words = words + (append_zero ? 1U : 0U);
    payload_length = word_bytes * (size_t)words;
    length = word_bytes * (size_t)wire_words;
    tx = malloc(length);
    if (!tx)
        return -1;
    if (et_build_pattern_sequence(bits, tx, wire_words, sequence, ET_STREAM_MASTER) != length) {
        free(tx);
        return -1;
    }

    if (direction != TEST_RX) {
        rx = calloc(1, length);
        expected = calloc(1, length);
        if (!rx || !expected ||
            et_build_pattern_sequence(bits, expected, words, sequence, ET_STREAM_SLAVE) != payload_length) {
            free(expected);
            free(rx);
            free(tx);
            return -1;
        }
    }

    if (configure_spi(fd, bits, speed_hz) < 0) {
        suite_set_error("case=%u spidev configuration failed", sequence);
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
        suite_set_error("case=%u SPI_IOC_MESSAGE failed", sequence);
        free(expected);
        free(rx);
        free(tx);
        return -1;
    }
    if (ret != (int)length) {
        fprintf(stderr, "[master] case=%u bits=%u transferred=%d expected=%zu\n",
                sequence, bits, ret, length);
        suite_set_error("case=%u short transfer %d/%zu", sequence, ret, length);
        free(expected);
        free(rx);
        free(tx);
        return -1;
    }

    report_printf("[master] case=%u direction=%s bits=%u words=%u bytes=%zu sent; cs released",
                  sequence, direction_name(direction), bits, wire_words, length);
    if (direction != TEST_RX) {
        result = et_compare(bits, expected, rx, wire_words);
        report_printf(" miso=%s\n", et_result_name(result));
        if (result != ET_MATCH) {
            suite_set_error("case=%u MISO=%s", sequence, et_result_name(result));
            report_printf("[master] expected miso: ");
            print_bytes(expected, length);
            report_printf("[master] actual miso:   ");
            print_bytes(rx, length);
        }
    } else {
        report_printf("\n");
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

static int run_suite_group(int fd,
                           const char *name,
                           const unsigned int *widths,
                           size_t width_count,
                           unsigned int words,
                           unsigned int loops,
                           unsigned int speed_hz,
                           test_direction_t direction,
                           int append_zero,
                           unsigned int *sequence)
{
    unsigned int loop;
    size_t i;
    int passed = 1;
    suite_last_error[0] = '\0';

    for (loop = 0; loop < loops; loop++) {
        for (i = 0; i < width_count; i++) {
            if (send_case(fd, widths[i], speed_hz, words, *sequence, direction, append_zero) < 0)
                passed = 0;
            (*sequence)++;
            if (suite_case_delay_ms && wait_ms(suite_case_delay_ms) < 0)
                passed = 0;
        }
    }
    suite_result(name, passed, passed ? "all cases passed" :
                 (suite_last_error[0] ? suite_last_error : "one or more cases failed"));
    return passed ? 0 : -1;
}

static int run_master_config_check(int fd)
{
    int passed = configure_spi(fd, 8, DEFAULT_SPEED_HZ) == 0;

    suite_result("01_config", passed,
                 passed ? "mode=3, MSB-first, bpw=8 accepted" : "spidev configuration failed");
    return passed ? 0 : -1;
}

static int run_master_suite(int fd, unsigned int speed_hz)
{
    static const unsigned int all_widths[] = { 8U, 16U, 32U };
    static const unsigned int rx_width[] = { 8U };
    static const unsigned int speeds[] = { 100000U, 1000000U, 10000000U, 25000000U, 40000000U };
    unsigned int sequence = 0U;
    char speed_name[32];
    size_t i;
    int passed = 1;

    if (run_master_config_check(fd) < 0)
        passed = 0;
    if (run_suite_group(fd, "02_basic_rx", all_widths, 3, 8, 1, speed_hz,
                        TEST_RX, 0, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "03_rearm", all_widths, 3, 8, 5, speed_hz,
                        TEST_RX, 0, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "04_long_rx", all_widths, 3, 128, 2, speed_hz,
                        TEST_RX, 0, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "05_slave_tx_widths", all_widths, 3, 8, 1, speed_hz,
                        TEST_TX, 0, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "06_slave_tx_min", all_widths, 3, 1, 1, speed_hz,
                        TEST_TX, 0, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "07_slave_tx_fifo_limit", all_widths, 3, 64, 1, speed_hz,
                        TEST_TX, 0, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "08_slave_tx_append_zero", all_widths, 3, 8, 1, speed_hz,
                        TEST_TX, 1, &sequence) < 0)
        passed = 0;
    if (run_suite_group(fd, "09_duplex_widths", all_widths, 3, 8, 1, speed_hz,
                        TEST_DUPLEX, 0, &sequence) < 0)
        passed = 0;

    for (i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
        snprintf(speed_name, sizeof(speed_name), "10_speed_%uHz", speeds[i]);
        if (run_suite_group(fd, speed_name, rx_width, 1, 8, 1, speeds[i],
                            TEST_RX, 0, &sequence) < 0)
            passed = 0;
    }
    suite_result("overall", passed, passed ? "all ten groups passed" : "one or more groups failed");
    return passed ? 0 : -1;
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
    const char *log_path = NULL;
    int suite_mode = 0;
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
        } else if (!strcmp(argv[i], "--suite")) {
            suite_mode = 1;
        } else if (!strcmp(argv[i], "--log") && ++i < (size_t)argc) {
            log_path = argv[i];
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

    if (suite_mode) {
        suite_case_delay_ms = case_delay_ms;
        if (!open_suite_report(argv[0], log_path)) {
            close(fd);
            return 1;
        }
        report_printf("[master] suite device=%s; start slave suite first\n", device);
        if (start_delay_ms && wait_ms(start_delay_ms) < 0) {
            fclose(suite_report);
            suite_report = NULL;
            close(fd);
            return 1;
        }
        if (run_master_suite(fd, speed_hz) < 0) {
            fprintf(suite_report, "\nResult: FAIL (pass=%u fail=%u)\n", suite_passed, suite_failed);
            fclose(suite_report);
            suite_report = NULL;
            close(fd);
            return 1;
        }
        fprintf(suite_report, "\nResult: PASS (pass=%u fail=%u)\n", suite_passed, suite_failed);
        fclose(suite_report);
        suite_report = NULL;
        close(fd);
        return 0;
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
            if (send_case(fd, widths[i], speed_hz, words, sequence, direction, 0) < 0) {
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
