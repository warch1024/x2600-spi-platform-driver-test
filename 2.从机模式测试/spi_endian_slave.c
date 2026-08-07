#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libhardware2/sslv.h>

#include "pattern.h"

#define DEFAULT_DEVICE "/dev/sslv0"
#define DEFAULT_LOOPS 1U
#define DEFAULT_WORDS 8U
#define MAX_WORDS 4096U
#define MAX_TX_WORDS 64U

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
            "Usage: %s [--device PATH] [--loops N] [--bits 8|16|32] "
            "[--words N] [--direction rx|tx|duplex] [--config-check]\n",
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

static void print_bytes(const uint8_t *data, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
        printf("%02x%s", data[i], i + 1U == length ? "" : " ");
    putchar('\n');
}

static int expect_failure(int ret, const char *operation)
{
    if (ret >= 0) {
        fprintf(stderr, "[slave] config check unexpectedly succeeded: %s\n", operation);
        return -1;
    }
    return 0;
}

static int run_config_check(int fd)
{
    static const int invalid_modes[] = { 0, 1, 2 };
    size_t i;

    for (i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]); i++) {
        if (expect_failure(sslv_set_mode(fd, invalid_modes[i]), "set invalid mode") < 0)
            return -1;
    }
    if (expect_failure(sslv_set_bits(fd, 3), "set bits=3") < 0 ||
        expect_failure(sslv_set_bits(fd, 33), "set bits=33") < 0 ||
        sslv_set_mode(fd, 3) < 0 || sslv_set_bits(fd, 8) < 0 ||
        sslv_enable(fd) < 0)
        return -1;

    if (expect_failure(sslv_set_mode(fd, 3), "set mode while busy") < 0 ||
        expect_failure(sslv_set_bits(fd, 16), "set bits while busy") < 0) {
        sslv_disable(fd);
        return -1;
    }
    if (sslv_disable(fd) < 0) {
        fprintf(stderr, "[slave] config check failed to disable\n");
        return -1;
    }

    puts("[slave] configuration checks passed");
    return 0;
}

static int receive_case(int fd,
                        unsigned int bits,
                        unsigned int words,
                        unsigned int sequence,
                        test_direction_t direction)
{
    uint8_t *expected = NULL;
    uint8_t *actual = NULL;
    uint8_t *tx = NULL;
    et_result_t result;
    size_t length = et_word_bytes(bits) * (size_t)words;
    int ret;

    if (!length || sslv_set_bits(fd, (int)bits) < 0 || sslv_enable(fd) < 0)
        return -1;

    actual = calloc(1, length);
    if (!actual)
        goto fail;

    if (direction != TEST_RX) {
        tx = malloc(length);
        if (!tx || et_build_pattern_sequence(bits, tx, words, sequence, ET_STREAM_SLAVE) != length)
            goto fail;
        if (sslv_send(fd, tx, (int)length, 0) != (int)length)
            goto fail;
    }

    printf("[slave] case=%u direction=%s bits=%u words=%u ready; start master now\n",
           sequence, direction_name(direction), bits, words);
    fflush(stdout);
    ret = sslv_receive(fd, actual, (int)length);
    if (sslv_disable(fd) < 0)
        goto fail;
    if (ret != (int)length) {
        fprintf(stderr, "[slave] case=%u bits=%u receive=%d expected=%zu\n",
                sequence, bits, ret, length);
        free(tx);
        free(actual);
        return -1;
    }

    if (direction == TEST_TX) {
        printf("[slave] case=%u direction=tx bits=%u words=%u result=clocked\n",
               sequence, bits, words);
        free(tx);
        free(actual);
        return 0;
    }

    expected = malloc(length);
    if (!expected ||
        et_build_pattern_sequence(bits, expected, words, sequence, ET_STREAM_MASTER) != length)
        goto fail_after_disable;
    result = et_compare(bits, expected, actual, words);
    printf("[slave] case=%u direction=%s bits=%u words=%u result=%s\n",
           sequence, direction_name(direction), bits, words, et_result_name(result));
    if (result != ET_MATCH) {
        printf("[slave] expected: ");
        print_bytes(expected, length);
        printf("[slave] actual:   ");
        print_bytes(actual, length);
    }

    free(expected);
    free(tx);
    free(actual);
    return result == ET_MATCH ? 0 : -1;

fail:
    sslv_disable(fd);
fail_after_disable:
    free(expected);
    free(tx);
    free(actual);
    return -1;
}

int main(int argc, char **argv)
{
    static const unsigned int widths[] = { 8U, 16U, 32U };
    const char *device = DEFAULT_DEVICE;
    unsigned int loops = DEFAULT_LOOPS;
    unsigned int words = DEFAULT_WORDS;
    unsigned int selected_bits = 0U;
    unsigned int sequence = 0U;
    test_direction_t direction = TEST_RX;
    int config_check = 0;
    unsigned int loop;
    size_t i;
    int fd;

    for (i = 1; i < (size_t)argc; i++) {
        if (!strcmp(argv[i], "--device") && ++i < (size_t)argc) {
            device = argv[i];
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
        } else if (!strcmp(argv[i], "--config-check")) {
            config_check = 1;
        } else {
            return usage(argv[0]), 1;
        }
    }

    if (direction != TEST_RX && words > MAX_TX_WORDS) {
        fprintf(stderr, "[slave] tx/duplex currently supports at most %u words\n", MAX_TX_WORDS);
        return 1;
    }

    fd = sslv_open((char *)device);
    if (fd < 0)
        return 1;
    if (config_check) {
        int ret = run_config_check(fd);
        sslv_close(fd);
        return ret < 0 ? 1 : 0;
    }
    if (sslv_set_mode(fd, 3) < 0) {
        sslv_close(fd);
        return 1;
    }

    printf("[slave] device=%s mode=3 loops=%u bits=%s words=%u direction=%s; "
           "start master now\n",
           device, loops, selected_bits ? "selected" : "all", words,
           direction_name(direction));
    fflush(stdout);

    for (loop = 0; loop < loops; loop++) {
        for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
            if (selected_bits && widths[i] != selected_bits)
                continue;
            if (receive_case(fd, widths[i], words, sequence, direction) < 0) {
                sslv_close(fd);
                return 1;
            }
            sequence++;
        }
    }

    sslv_close(fd);
    puts("[slave] all requested cases matched");
    return 0;
}
