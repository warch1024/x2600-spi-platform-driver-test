#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libhardware2/sslv.h>

#include "pattern.h"

#define DEFAULT_DEVICE "/dev/sslv0"
#define DEFAULT_LOOPS 1U
#define DEFAULT_WORDS 8U
#define MAX_WORDS 4096U
#define MAX_TX_WORDS 64U
#define SUITE_CASE_TIMEOUT_SEC 15U

typedef enum {
    TEST_RX = 0,
    TEST_TX,
    TEST_DUPLEX,
} test_direction_t;

static FILE *suite_report;
static unsigned int suite_passed;
static unsigned int suite_failed;
static unsigned int suite_timeout_sec;
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
            snprintf(default_path, sizeof(default_path), "%.*s/spi_endian_slave_suite_%s.md",
                     (int)(slash - argv0), argv0, timestamp);
        else
            snprintf(default_path, sizeof(default_path), "./spi_endian_slave_suite_%s.md", timestamp);
        path = default_path;
    }

    suite_report = fopen(path, "w");
    if (!suite_report) {
        perror(path);
        return NULL;
    }
    fprintf(suite_report, "# SPI SSLV Slave Suite Report\n\n");
    fprintf(suite_report, "## Results\n\n");
    report_printf("[suite] report=%s\n", path);
    return suite_report;
}

static void alarm_handler(int signal_number)
{
    (void)signal_number;
}

static int install_alarm_handler(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = alarm_handler;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGALRM, &action, NULL);
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
            "Usage: %s [--device PATH] [--loops N] [--bits 8|16|32] "
            "[--words N] [--direction rx|tx|duplex] [--config-check] "
            "[--suite] [--log PATH]\n",
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
    if (suite_report) {
        for (i = 0; i < length; i++)
            fprintf(suite_report, "%02x%s", data[i], i + 1U == length ? "" : " ");
        fputc('\n', suite_report);
    }
}

static int expect_failure(int ret, const char *operation)
{
    if (ret >= 0) {
        fprintf(stderr, "[slave] config check unexpectedly succeeded: %s\n", operation);
        return -1;
    }
    return 0;
}

static int expect_info(int fd, unsigned int expected_bits, const char *operation)
{
    struct sslv_config_data info;

    memset(&info, 0xa5, sizeof(info));
    sslv_get_info(fd, &info);
    if (info.id != 0U || info.bits_per_word != expected_bits ||
        info.sslv_pol != 1U || info.sslv_pha != 1U || info.loop_mode != 0U) {
        fprintf(stderr,
                "[slave] config info mismatch after %s: id=%u bits=%u pol=%u pha=%u loop=%u\n",
                operation, info.id, info.bits_per_word, info.sslv_pol,
                info.sslv_pha, info.loop_mode);
        return -1;
    }
    return 0;
}

static int run_config_check(int fd)
{
    static const int invalid_modes[] = { 0, 1, 2 };
    size_t i;
    int passed = 1;

    for (i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]); i++) {
        if (expect_failure(sslv_set_mode(fd, invalid_modes[i]), "set invalid mode") < 0)
            passed = 0;
    }
    if (expect_failure(sslv_set_bits(fd, 3), "set bits=3") < 0 ||
        expect_failure(sslv_set_bits(fd, 33), "set bits=33") < 0 ||
        sslv_set_mode(fd, 3) < 0 ||
        sslv_set_bits(fd, 4) < 0 || expect_info(fd, 4, "set bits=4") < 0 ||
        sslv_set_bits(fd, 32) < 0 || expect_info(fd, 32, "set bits=32") < 0 ||
        sslv_set_bits(fd, 8) < 0 || expect_info(fd, 8, "set bits=8") < 0 ||
        sslv_enable(fd) < 0 || expect_info(fd, 8, "enable") < 0)
        passed = 0;

    if (expect_failure(sslv_set_mode(fd, 3), "set mode while busy") < 0 ||
        expect_failure(sslv_set_bits(fd, 16), "set bits while busy") < 0) {
        sslv_disable(fd);
        passed = 0;
    }
    if (sslv_disable(fd) < 0) {
        fprintf(stderr, "[slave] config check failed to disable\n");
        passed = 0;
    }
    if (expect_info(fd, 8, "disable") < 0)
        passed = 0;

    if (suite_report)
        suite_result("01_config", passed, passed ? "invalid settings rejected" : "unexpected configuration result");
    else if (passed)
        puts("[slave] configuration checks passed");
    return passed ? 0 : -1;
}

static int receive_case(int fd,
                        unsigned int bits,
                        unsigned int words,
                        unsigned int sequence,
                        test_direction_t direction,
                        int append_zero)
{
    uint8_t *expected = NULL;
    uint8_t *actual = NULL;
    uint8_t *tx = NULL;
    et_result_t result;
    size_t word_bytes = et_word_bytes(bits);
    unsigned int wire_words = words + (append_zero ? 1U : 0U);
    size_t payload_length = word_bytes * (size_t)words;
    size_t length = word_bytes * (size_t)wire_words;
    int send_ok = 1;
    int ret;

    if (!length || sslv_set_bits(fd, (int)bits) < 0 || sslv_enable(fd) < 0)
        return -1;

    actual = calloc(1, length);
    if (!actual)
        goto fail;

    if (direction != TEST_RX) {
        tx = malloc(payload_length);
        if (!tx || et_build_pattern_sequence(bits, tx, words, sequence, ET_STREAM_SLAVE) != payload_length)
            goto fail;
        ret = sslv_send(fd, tx, (int)payload_length, (char)append_zero);
        if (ret < 0) {
            suite_set_error("case=%u slave TX failed", sequence);
            goto fail;
        }
        if (ret != (int)payload_length) {
            suite_set_error("case=%u slave TX returned %d expected=%zu",
                            sequence, ret, payload_length);
            send_ok = 0;
        }
    }

    report_printf("[slave] case=%u direction=%s bits=%u words=%u bytes=%zu ready; start master now\n",
                  sequence, direction_name(direction), bits, wire_words, length);
    fflush(stdout);
    if (suite_timeout_sec)
        alarm(suite_timeout_sec);
    ret = sslv_receive(fd, actual, (int)length);
    if (suite_timeout_sec)
        alarm(0);
    if (sslv_disable(fd) < 0)
        goto fail;
    if (ret != (int)length) {
        suite_set_error("case=%u receive=%d expected=%zu", sequence, ret, length);
        report_printf("[slave] case=%u bits=%u receive=%d expected=%zu\n",
                      sequence, bits, ret, length);
        free(tx);
        free(actual);
        return -1;
    }

    if (direction == TEST_TX) {
        report_printf("[slave] case=%u direction=tx bits=%u words=%u result=clocked\n",
                      sequence, bits, wire_words);
        free(tx);
        free(actual);
        return send_ok ? 0 : -1;
    }

    expected = calloc(1, length);
    if (!expected ||
        et_build_pattern_sequence(bits, expected, words, sequence, ET_STREAM_MASTER) != payload_length)
        goto fail_after_disable;
    result = et_compare(bits, expected, actual, wire_words);
    report_printf("[slave] case=%u direction=%s bits=%u words=%u result=%s\n",
                  sequence, direction_name(direction), bits, wire_words, et_result_name(result));
    if (!send_ok || result != ET_MATCH) {
        if (!send_ok)
            report_printf("[slave] case=%u TX return did not match requested payload length\n", sequence);
        suite_set_error("case=%u MOSI=%s", sequence, et_result_name(result));
        printf("[slave] expected: ");
        print_bytes(expected, length);
        printf("[slave] actual:   ");
        print_bytes(actual, length);
    }

    free(expected);
    free(tx);
    free(actual);
    return send_ok && result == ET_MATCH ? 0 : -1;

fail:
    sslv_disable(fd);
fail_after_disable:
    free(expected);
    free(tx);
    free(actual);
    return -1;
}

static int run_slave_group(int fd,
                           const char *name,
                           const unsigned int *widths,
                           size_t width_count,
                           unsigned int words,
                           unsigned int loops,
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
            if (receive_case(fd, widths[i], words, *sequence, direction, append_zero) < 0)
                passed = 0;
            (*sequence)++;
        }
    }
    suite_result(name, passed, passed ? "all cases passed" :
                 (suite_last_error[0] ? suite_last_error : "one or more cases failed"));
    return passed ? 0 : -1;
}

static int run_slave_suite(int fd)
{
    static const unsigned int all_widths[] = { 8U, 16U, 32U };
    static const unsigned int speed_width[] = { 8U };
    unsigned int sequence = 0U;
    unsigned int speed_index;
    static const unsigned int speeds[] = { 100000U, 1000000U, 10000000U, 25000000U, 40000000U };
    char speed_name[32];
    int passed = 1;

    if (run_config_check(fd) < 0)
        passed = 0;
    if (run_slave_group(fd, "02_basic_rx", all_widths, 3, 8, 1, TEST_RX, 0, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "03_rearm", all_widths, 3, 8, 5, TEST_RX, 0, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "04_long_rx", all_widths, 3, 128, 2, TEST_RX, 0, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "05_slave_tx_widths", all_widths, 3, 8, 1, TEST_TX, 0, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "06_slave_tx_min", all_widths, 3, 1, 1, TEST_TX, 0, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "07_slave_tx_fifo_limit", all_widths, 3, 64, 1, TEST_TX, 0, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "08_slave_tx_append_zero", all_widths, 3, 8, 1, TEST_TX, 1, &sequence) < 0)
        passed = 0;
    if (run_slave_group(fd, "09_duplex_widths", all_widths, 3, 8, 1, TEST_DUPLEX, 0, &sequence) < 0)
        passed = 0;
    for (speed_index = 0; speed_index < 5U; speed_index++) {
        snprintf(speed_name, sizeof(speed_name), "10_speed_%uHz", speeds[speed_index]);
        if (run_slave_group(fd, speed_name, speed_width, 1, 8, 1,
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
    unsigned int loops = DEFAULT_LOOPS;
    unsigned int words = DEFAULT_WORDS;
    unsigned int selected_bits = 0U;
    unsigned int sequence = 0U;
    test_direction_t direction = TEST_RX;
    int config_check = 0;
    int suite_mode = 0;
    const char *log_path = NULL;
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
        } else if (!strcmp(argv[i], "--suite")) {
            suite_mode = 1;
        } else if (!strcmp(argv[i], "--log") && ++i < (size_t)argc) {
            log_path = argv[i];
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

    if (suite_mode) {
        if (install_alarm_handler() < 0) {
            perror("sigaction");
            sslv_close(fd);
            return 1;
        }
        suite_timeout_sec = SUITE_CASE_TIMEOUT_SEC;
        if (!open_suite_report(argv[0], log_path)) {
            sslv_close(fd);
            return 1;
        }
        report_printf("[slave] suite device=%s; wait for master suite\n", device);
        if (run_slave_suite(fd) < 0) {
            fprintf(suite_report, "\nResult: FAIL (pass=%u fail=%u)\n", suite_passed, suite_failed);
            fclose(suite_report);
            suite_report = NULL;
            sslv_close(fd);
            return 1;
        }
        fprintf(suite_report, "\nResult: PASS (pass=%u fail=%u)\n", suite_passed, suite_failed);
        fclose(suite_report);
        suite_report = NULL;
        sslv_close(fd);
        return 0;
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
            if (receive_case(fd, widths[i], words, sequence, direction, 0) < 0) {
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
