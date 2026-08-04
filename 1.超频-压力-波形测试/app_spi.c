#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <libhardware2/gpio.h>
#include <libhardware2/spi.h>

#include "spi_clock.h"
#include "spi_stats.h"
#include "spi_stress.h"
#include "spi_timing.h"

#define DEFAULT_BUS          1U
#define DEFAULT_CS           "pc30"
#define DEFAULT_HW_CS        "pc30"
#define DEFAULT_MAX_TRANSFER (1024U * 1024U)
#define DEFAULT_SCAN_LOOPS   4U
#define DEFAULT_CS_ARM_MS    2000U
#define DEVICE_WAIT_US       20000U
#define DEVICE_WAIT_RETRIES  150U
#define SSI_IO_MAX_HZ        80000000U
#define SSI_SOURCE_60MHZ     120000000U
#define TEST_SCLK_HZ         60000000U
#define STRESS_LENGTH        (64U * 1024U)

typedef enum {
    TEST_MODE_COMPLETE,
    TEST_MODE_DELAY,
    TEST_MODE_ALWAYS_SPEED,
    TEST_MODE_MAX_SCLK,
} test_mode_t;

typedef struct {
    int fd;
    unsigned int bus;
    char path[20];
    char cs[16];
    size_t spidev_bufsiz;
} spi_context_t;

typedef struct {
    unsigned int bus;
    char cs[16];
    char hw_cs[16];
    unsigned int max_transfer;
    unsigned int scan_loops;
    unsigned int cs_arm_ms;
    unsigned int cs_to_clk_ns;
    unsigned int ssi_source_hz;
    unsigned int div_ssi_rate;
    unsigned int always_speed;
    unsigned int stress_seconds;
    int cs_to_clk_ns_valid;
    int bus_specified;
    int cs_specified;
    int hw_cs_specified;
    int qualification;
    int stress;
    test_mode_t mode;
    const char *report_name;
} test_options_t;

typedef struct {
    FILE *fp;
    FILE *samples_fp;
    char path[PATH_MAX];
    char samples_path[PATH_MAX];
    uint64_t pass;
    uint64_t fail;
    uint64_t skip;
    uint64_t transfers;
    uint64_t bytes;
    uint64_t errors;
    int output_failed;
} test_report_t;

typedef struct {
    unsigned int bus;
    const char *cs;
    const char *clk;
    const char *mosi;
    const char *miso;
} qualification_profile_t;

typedef struct {
    unsigned int highest_speed;
    double payload_mbps;
} scan_result_t;

static const qualification_profile_t qualification_profiles[] = {
    { 0U, "pc09", "PD00", "PD01", "PD02" },
    { 1U, "pc30", "PC25", "PC26", "PC27" },
};

static const unsigned int scan_speeds[] = {
    1000000U, 5000000U, 10000000U, 20000000U, 25000000U, 50000000U, TEST_SCLK_HZ,
};

static const size_t primary_lengths[] = {
    16U * 1024U,
    128U * 1024U,
    1024U * 1024U,
};

static const size_t boundary_lengths[] = {
    127U, 128U, 129U, 4095U, 4096U, 4097U,
};

static volatile sig_atomic_t stop_requested;
static test_report_t report;

static unsigned int max_supported_sclk(unsigned int source_hz);

static int add_u64(uint64_t *value, uint64_t amount)
{
    if (UINT64_MAX - *value < amount)
        return -EOVERFLOW;
    *value += amount;
    return 0;
}

static double elapsed_seconds(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void on_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static const char *mode_name(const test_options_t *opt)
{
    if (opt->qualification)
        return "60MHz 资格测试";
    if (opt->stress)
        return "60MHz 独立压力测试";
    if (opt->mode == TEST_MODE_DELAY)
        return "CS 时序测量";
    if (opt->mode == TEST_MODE_ALWAYS_SPEED)
        return "连续频率发送";
    if (opt->mode == TEST_MODE_MAX_SCLK)
        return "CGV=0 最大 SCLK 单档测试";
    return "快速完整测试";
}

static void report_vprintf(const char *fmt, va_list ap)
{
    if (report.fp) {
        if (vfprintf(report.fp, fmt, ap) < 0 || fflush(report.fp) != 0)
            report.output_failed = 1;
    }
}

static void report_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    report_vprintf(fmt, ap);
    va_end(ap);
}

static void case_result(const char *category, const char *name, int status, const char *detail)
{
    const char *word = status > 0 ? "通过" : status < 0 ? "失败" : "跳过";

    if (status > 0)
        add_u64(&report.pass, 1);
    else if (status < 0)
        add_u64(&report.fail, 1);
    else
        add_u64(&report.skip, 1);
    if (status <= 0)
        printf("[%s] %s/%s%s%s\n", word, category, name, detail && detail[0] ? " - " : "", detail && detail[0] ? detail : "");
    report_printf("| %s | %s | %s | %s |\n", word, category, name, detail && detail[0] ? detail : "");
}

static void record_transfer(size_t len, int data_error)
{
    if (add_u64(&report.transfers, 1) < 0 || add_u64(&report.bytes, (uint64_t)len) < 0 ||
        (data_error && add_u64(&report.errors, 1) < 0))
        report.errors = UINT64_MAX;
}

static int report_open(const test_options_t *opt)
{
    struct timespec now;
    struct tm tmv;
    time_t wall;

    memset(&report, 0, sizeof(report));
    if (opt->report_name) {
        snprintf(report.path, sizeof(report.path), "%s", opt->report_name);
    } else {
        wall = time(NULL);
        localtime_r(&wall, &tmv);
        snprintf(report.path,
                 sizeof(report.path),
                 "/tmp/spi_test_report_%04d%02d%02d_%02d%02d%02d.md",
                 tmv.tm_year + 1900,
                 tmv.tm_mon + 1,
                 tmv.tm_mday,
                 tmv.tm_hour,
                 tmv.tm_min,
                 tmv.tm_sec);
    }
    report.fp = fopen(report.path, "w");
    if (!report.fp) {
        fprintf(stderr, "无法创建报告 %s: %s\n", report.path, strerror(errno));
        return -1;
    }
    if (opt->qualification) {
        if (snprintf(report.samples_path, sizeof(report.samples_path), "%s.csv", report.path) >=
                (int)sizeof(report.samples_path) ||
            !(report.samples_fp = fopen(report.samples_path, "w"))) {
            fprintf(stderr, "无法创建样本 CSV %s: %s\n", report.samples_path, strerror(errno));
            fclose(report.fp);
            report.fp = NULL;
            return -1;
        }
        if (fprintf(report.samples_fp,
                    "bus,phase,length_bytes,sample,elapsed_ms,"
                    "payload_bytes,mbit_per_s\n") < 0 ||
            fflush(report.samples_fp) != 0) {
            fclose(report.samples_fp);
            fclose(report.fp);
            report.samples_fp = NULL;
            report.fp = NULL;
            return -1;
        }
    }
    clock_gettime(CLOCK_REALTIME, &now);
    report_printf("# X2600 SPI 测试报告\n\n");
    report_printf("- 生成时间: %s", ctime(&now.tv_sec));
    report_printf("- 测试模式: `%s`\n", mode_name(opt));
    report_printf("- 驱动源码: "
                  "`/home/devvean/work/linux/module_driver/soc/x2600_510/spi/spi.c`\n");
    if (opt->stress) {
        report_printf("- SCLK: %u Hz；随机缓冲区: %u KiB；压力时长: %u 秒；SPI 总线: %u；CS: %s\n\n",
                      TEST_SCLK_HZ,
                      STRESS_LENGTH / 1024U,
                      opt->stress_seconds,
                      opt->bus,
                      opt->cs);
    } else if (opt->mode == TEST_MODE_MAX_SCLK) {
        spi_clock_plan_t plan;

        if (spi_clock_plan_from_mpll(opt->div_ssi_rate, &plan) == 0)
            report_printf("- MPLL: %" PRIu64 " Hz；输入 div_ssi_rate: %" PRIu64
                          " Hz；SSICDR 分频: %u；理论实际 div_ssi: %" PRIu64
                          " Hz；CGV: 0；理论实际 SCLK: %" PRIu64 " Hz\n\n",
                          SPI_MPLL_HZ,
                          plan.requested_div_ssi_hz,
                          plan.ssicdr_divisor,
                          plan.actual_div_ssi_hz,
                          plan.actual_sclk_hz);
    } else {
        report_printf("- SCLK 目标: 60000000 Hz；SSI 源时钟参数: %u Hz\n\n", opt->ssi_source_hz);
    }
    report_printf("## 结果明细\n\n| 状态 | 类别 | 用例 | 详情 |\n|---|---|---|---|\n");
    return 0;
}

static int report_close(void)
{
    int failed = report.output_failed;

    if (!report.fp)
        return failed ? -1 : 0;
    report_printf("\n## 汇总\n\n- 通过: %" PRIu64 "\n- 失败: %" PRIu64 "\n- 跳过: %" PRIu64 "\n- transfer 次数: %" PRIu64
                  "\n- 检查字节: %" PRIu64 "\n- 数据错误: %" PRIu64 "\n",
                  report.pass,
                  report.fail,
                  report.skip,
                  report.transfers,
                  report.bytes,
                  report.errors);
    if (report.output_failed)
        failed = 1;
    if (fclose(report.fp) != 0)
        failed = 1;
    report.fp = NULL;
    if (report.samples_fp) {
        if (fclose(report.samples_fp) != 0)
            failed = 1;
        report.samples_fp = NULL;
    }
    report.output_failed = failed;
    return failed ? -1 : 0;
}

static int parse_uint(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno || end == text || *end || parsed > UINT_MAX)
        return -1;
    *value = (unsigned int)parsed;
    return 0;
}

static int parse_mode(const char *text, test_mode_t *mode)
{
    if (!strcmp(text, "complete"))
        *mode = TEST_MODE_COMPLETE;
    else if (!strcmp(text, "delay"))
        *mode = TEST_MODE_DELAY;
    else if (!strcmp(text, "max-sclk"))
        *mode = TEST_MODE_MAX_SCLK;
    else
        return -1;
    return 0;
}

static int parse_args(int argc, char **argv, test_options_t *opt)
{
    int i;

    memset(opt, 0, sizeof(*opt));
    opt->bus = DEFAULT_BUS;
    snprintf(opt->cs, sizeof(opt->cs), "%s", DEFAULT_CS);
    snprintf(opt->hw_cs, sizeof(opt->hw_cs), "%s", DEFAULT_HW_CS);
    opt->max_transfer = DEFAULT_MAX_TRANSFER;
    opt->scan_loops = DEFAULT_SCAN_LOOPS;
    opt->cs_arm_ms = DEFAULT_CS_ARM_MS;
    opt->mode = TEST_MODE_COMPLETE;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
            return 1;
        if (!strcmp(argv[i], "--qualification")) {
            opt->qualification = 1;
            continue;
        }
        if (!strcmp(argv[i], "--stress")) {
            opt->stress = 1;
            continue;
        }
        if (i + 1 >= argc)
            return -1;
        if (!strcmp(argv[i], "--mode")) {
            if (!strcmp(argv[++i], "always-speed")) {
                opt->mode = TEST_MODE_ALWAYS_SPEED;
                if (++i >= argc || parse_uint(argv[i], &opt->always_speed) < 0 || !opt->always_speed)
                    return -1;
            } else if (parse_mode(argv[i], &opt->mode) < 0) {
                return -1;
            }
        } else if (!strcmp(argv[i], "--bus")) {
            if (parse_uint(argv[++i], &opt->bus) < 0)
                return -1;
            opt->bus_specified = 1;
        } else if (!strcmp(argv[i], "--cs")) {
            if (strlen(argv[++i]) >= sizeof(opt->cs))
                return -1;
            snprintf(opt->cs, sizeof(opt->cs), "%s", argv[i]);
            opt->cs_specified = 1;
        } else if (!strcmp(argv[i], "--hw-cs")) {
            if (strlen(argv[++i]) >= sizeof(opt->hw_cs))
                return -1;
            snprintf(opt->hw_cs, sizeof(opt->hw_cs), "%s", argv[i]);
            opt->hw_cs_specified = 1;
        } else if (!strcmp(argv[i], "--max-transfer")) {
            if (parse_uint(argv[++i], &opt->max_transfer) < 0 || !opt->max_transfer)
                return -1;
        } else if (!strcmp(argv[i], "--loops")) {
            if (parse_uint(argv[++i], &opt->scan_loops) < 0 || !opt->scan_loops)
                return -1;
        } else if (!strcmp(argv[i], "--cs-arm-ms")) {
            if (parse_uint(argv[++i], &opt->cs_arm_ms) < 0 || opt->cs_arm_ms > 60000U)
                return -1;
        } else if (!strcmp(argv[i], "--cs-to-clk-ns")) {
            if (parse_uint(argv[++i], &opt->cs_to_clk_ns) < 0)
                return -1;
            opt->cs_to_clk_ns_valid = 1;
        } else if (!strcmp(argv[i], "--ssi-source-hz")) {
            if (parse_uint(argv[++i], &opt->ssi_source_hz) < 0 || !opt->ssi_source_hz)
                return -1;
        } else if (!strcmp(argv[i], "--div-ssi-rate")) {
            if (parse_uint(argv[++i], &opt->div_ssi_rate) < 0 || !opt->div_ssi_rate)
                return -1;
        } else if (!strcmp(argv[i], "--stress-seconds")) {
            if (parse_uint(argv[++i], &opt->stress_seconds) < 0 ||
                !spi_stress_seconds_valid(opt->stress_seconds))
                return -1;
        } else if (!strcmp(argv[i], "--report")) {
            opt->report_name = argv[++i];
        } else {
            return -1;
        }
    }
    if (opt->max_transfer < 8U)
        return -1;
    if (opt->mode == TEST_MODE_ALWAYS_SPEED && opt->ssi_source_hz && opt->always_speed > max_supported_sclk(opt->ssi_source_hz))
        return -1;
    if (opt->stress) {
        if (opt->qualification || opt->mode != TEST_MODE_COMPLETE || opt->div_ssi_rate || opt->always_speed ||
            opt->ssi_source_hz || opt->hw_cs_specified || opt->bus > 1U)
            return -1;
        if (!opt->stress_seconds)
            opt->stress_seconds = SPI_STRESS_DEFAULT_SECONDS;
        if (!opt->cs_specified) {
            const char *default_cs = spi_stress_default_cs(opt->bus);

            if (!default_cs)
                return -1;
            snprintf(opt->cs, sizeof(opt->cs), "%s", default_cs);
        }
    } else if (opt->stress_seconds) {
        return -1;
    }
    if (opt->mode == TEST_MODE_MAX_SCLK) {
        spi_clock_plan_t plan;

        if (!opt->div_ssi_rate || opt->qualification || opt->ssi_source_hz ||
            spi_clock_plan_from_mpll(opt->div_ssi_rate, &plan) < 0)
            return -1;
    } else if (opt->div_ssi_rate) {
        return -1;
    }
    if (opt->qualification && (opt->mode == TEST_MODE_DELAY || opt->bus_specified || opt->cs_specified || opt->hw_cs_specified ||
                               opt->ssi_source_hz != SSI_SOURCE_60MHZ))
        return -1;
    return 0;
}

static void print_usage(const char *name)
{
    printf("用法: %s [选项]\n", name);
    printf("  --mode complete|delay|max-sclk  max-sclk 按 CGV=0 测试单档最高 SCLK\n");
    printf("  --mode complete|delay  complete 为默认快速全双工测试；delay 为 CS "
           "时序测试\n");
    printf("  --mode always-speed HZ 连续发送 0x55，供示波器观察 SCLK/MOSI\n");
    printf("  --qualification         自动完成 SPI0 后 SPI1 的 60MHz "
           "正式资格测试\n");
    printf("  --stress                独立执行 60MHz、64KiB 随机全双工压力测试\n");
    printf("  --stress-seconds N      --stress 持续时间，默认 %u 秒\n", SPI_STRESS_DEFAULT_SECONDS);
    printf("  --bus N                 SPI 总线；stress 支持 0/1，默认 %u\n", DEFAULT_BUS);
    printf("  --cs GPIO               软件 CS；stress 未指定时按总线选择默认 CS\n");
    printf("  --hw-cs GPIO            delay 中测硬件 CE0\n");
    printf("  --max-transfer N        complete/max-sclk 的逻辑负载字节数，默认 %u\n", DEFAULT_MAX_TRANSFER);
    printf("  --loops N               complete/max-sclk 每档图样轮数，默认 %u\n", DEFAULT_SCAN_LOOPS);
    printf("  --cs-arm-ms N           CS 仪器捕获等待时间，默认 %u ms\n", DEFAULT_CS_ARM_MS);
    printf("  --cs-to-clk-ns N        记录已测得的 CS 到首个 SCLK 间隔\n");
    printf("  --ssi-source-hz HZ      SSI 源时钟；qualification 必须为 120000000\n");
    printf("  --div-ssi-rate HZ       max-sclk 输入 MD_X2600_510_SPI_CLK_RATE，按 MPLL=1800000000 计算\n");
    printf("  --report FILE           Markdown 报告路径，默认 /tmp\n");
}

static size_t detect_spidev_bufsiz(void)
{
    FILE *fp;
    unsigned long value;

    fp = fopen("/sys/module/spidev/parameters/bufsiz", "r");
    if (!fp)
        return 4096U;
    if (fscanf(fp, "%lu", &value) != 1 || !value || value > SIZE_MAX) {
        fclose(fp);
        return 4096U;
    }
    fclose(fp);
    return (size_t)value;
}

static int read_text_value(const char *path, char *value, size_t size)
{
    FILE *fp;
    size_t len;

    fp = fopen(path, "r");
    if (!fp)
        return -1;
    if (!fgets(value, (int)size, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    len = strlen(value);
    while (len && (value[len - 1] == '\n' || value[len - 1] == '\r'))
        value[--len] = '\0';
    return 0;
}

static void print_driver_parameters(unsigned int bus)
{
    char names[4][24];
    const char *items[5];
    char path[PATH_MAX];
    char value[32];
    char detail[260];
    size_t i;
    size_t used = 0;

    snprintf(names[0], sizeof(names[0]), "spi%u_is_enable", bus);
    snprintf(names[1], sizeof(names[1]), "spi%u_clk", bus);
    snprintf(names[2], sizeof(names[2]), "spi%u_mosi", bus);
    snprintf(names[3], sizeof(names[3]), "spi%u_miso", bus);
    items[0] = "div_ssi_rate";
    items[1] = names[0];
    items[2] = names[1];
    items[3] = names[2];
    items[4] = names[3];
    detail[0] = '\0';
    for (i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        int written;

        if (snprintf(path, sizeof(path), "/sys/module/soc_spi/parameters/%s", items[i]) >= (int)sizeof(path) ||
            read_text_value(path, value, sizeof(value)) < 0)
            snprintf(value, sizeof(value), "(不可读取)");
        written = snprintf(detail + used, sizeof(detail) - used, "%s%s=%s", used ? " " : "", items[i], value);
        if (written < 0 || (size_t)written >= sizeof(detail) - used)
            break;
        used += (size_t)written;
    }
    printf("[诊断] %s\n", detail);
    report_printf("- 已加载 soc_spi: %s\n", detail);
}

static int spi_context_init(spi_context_t *ctx, unsigned int bus, const char *cs, size_t spidev_bufsiz)
{
    struct spidev_register_data data;
    unsigned int retry;

    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
    ctx->bus = bus;
    ctx->spidev_bufsiz = spidev_bufsiz ? spidev_bufsiz : 4096U;
    snprintf(ctx->cs, sizeof(ctx->cs), "%s", cs);
    memset(&data, 0, sizeof(data));
    data.busnum = (int)bus;
    data.cs_gpio = (char *)cs;
    if (spi_add_device(&data) < 0)
        return -1;
    for (retry = 0; retry < DEVICE_WAIT_RETRIES; retry++) {
        if (access(data.spidev_path, F_OK) == 0)
            break;
        usleep(DEVICE_WAIT_US);
    }
    ctx->fd = spi_open(data.spidev_path);
    if (ctx->fd < 0) {
        spi_del_device(data.spidev_path);
        return -1;
    }
    snprintf(ctx->path, sizeof(ctx->path), "%s", data.spidev_path);
    return 0;
}

static void spi_context_deinit(spi_context_t *ctx)
{
    if (ctx->fd >= 0)
        spi_close(ctx->fd);
    if (ctx->path[0])
        spi_del_device(ctx->path);
    ctx->fd = -1;
    ctx->path[0] = '\0';
}

static int configure_spi(spi_context_t *ctx, unsigned int mode, unsigned int speed)
{
    struct spi_info info;

    if (spi_set_mode(ctx->fd, (int)mode) < 0 || spi_set_speed(ctx->fd, (int)speed) < 0 || spi_set_bits(ctx->fd, 8) < 0 ||
        spi_set_lsb(ctx->fd, 0) < 0)
        return -1;
    memset(&info, 0, sizeof(info));
    spi_get_info(ctx->fd, &info);
    if (info.spi_bits != 8 || info.spi_lsb != 0 || info.spi_speed != speed ||
        (info.spi_mode & (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP)) !=
            (mode & (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP)))
        return -1;
    return 0;
}

static int raw_transfer(int fd, void *tx, void *rx, size_t len, unsigned int speed, unsigned int cs_change)
{
    struct spi_ioc_transfer message;

    memset(&message, 0, sizeof(message));
    message.tx_buf = (unsigned long)tx;
    message.rx_buf = (unsigned long)rx;
    message.len = (uint32_t)len;
    message.speed_hz = speed;
    message.bits_per_word = 8;
    message.cs_change = (uint8_t)cs_change;
    return spi_transfer(fd, &message, 1);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void fill_pattern(unsigned char *buffer, size_t len, unsigned int kind, uint32_t seed)
{
    size_t i;
    uint32_t state = seed;

    for (i = 0; i < len; i++) {
        switch (kind) {
        case 0:
            buffer[i] = 0x00;
            break;
        case 1:
            buffer[i] = 0xff;
            break;
        case 2:
            buffer[i] = 0xaa;
            break;
        case 3:
            buffer[i] = 0x55;
            break;
        default:
            buffer[i] = (unsigned char)next_random(&state);
            break;
        }
    }
}

static size_t first_mismatch(const unsigned char *tx, const unsigned char *rx, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (tx[i] != rx[i])
            return i;
    }
    return SIZE_MAX;
}

static int transfer_buffers(spi_context_t *ctx,
                            unsigned char *tx,
                            unsigned char *rx,
                            size_t len,
                            unsigned int speed,
                            size_t *bad_offset,
                            spi_timing_t *timing)
{
    size_t offset = 0;
    size_t limit = ctx->spidev_bufsiz ? ctx->spidev_bufsiz : 1U;

    while (offset < len) {
        size_t chunk = len - offset;
        int ret;
        struct timespec start;
        struct timespec end;

        if (chunk > limit)
            chunk = limit;
        if (timing)
            clock_gettime(CLOCK_MONOTONIC, &start);
        ret = raw_transfer(ctx->fd, tx + offset, rx + offset, chunk, speed, 0);
        if (timing) {
            clock_gettime(CLOCK_MONOTONIC, &end);
            if (ret == 0 && spi_timing_record(timing, chunk, speed, elapsed_seconds(&start, &end)) < 0)
                ret = -EOVERFLOW;
        }
        if (ret < 0) {
            if (bad_offset)
                *bad_offset = offset;
            return ret;
        }
        offset += chunk;
    }
    if (bad_offset)
        *bad_offset = first_mismatch(tx, rx, len);
    return bad_offset && *bad_offset != SIZE_MAX ? -EBADE : 0;
}

static int checked_transfer(spi_context_t *ctx,
                            unsigned char *tx,
                            unsigned char *rx,
                            size_t len,
                            unsigned int kind,
                            uint32_t seed,
                            size_t *bad_offset)
{
    int ret;

    fill_pattern(tx, len, kind, seed);
    memset(rx, 0xa5, len);
    ret = transfer_buffers(ctx, tx, rx, len, TEST_SCLK_HZ, bad_offset, NULL);
    record_transfer(len, ret != 0);
    return ret;
}

static int run_checked_once(spi_context_t *ctx, int internal_loop, size_t len, uint32_t seed, size_t *bad_offset)
{
    unsigned char *tx;
    unsigned char *rx;
    int ret;

    if (configure_spi(ctx, SPI_MODE_0 | (internal_loop ? SPI_LOOP : 0), TEST_SCLK_HZ) < 0)
        return -EINVAL;
    tx = malloc(len);
    rx = malloc(len);
    if (!tx || !rx) {
        free(tx);
        free(rx);
        return -ENOMEM;
    }
    ret = checked_transfer(ctx, tx, rx, len, 4U, seed, bad_offset);
    free(tx);
    free(rx);
    return ret;
}

static unsigned int max_supported_sclk(unsigned int source_hz)
{
    unsigned int divider;

    if (!source_hz)
        return 0;
    divider = (source_hz + 2U * SSI_IO_MAX_HZ - 1U) / (2U * SSI_IO_MAX_HZ);
    return source_hz / (2U * divider);
}

static scan_result_t run_frequency_scan(spi_context_t *ctx, const test_options_t *opt)
{
    scan_result_t result;
    unsigned int sclk_limit = max_supported_sclk(opt->ssi_source_hz);
    size_t i;

    memset(&result, 0, sizeof(result));
    if (sclk_limit) {
        char detail[180];

        snprintf(detail, sizeof(detail), "SSI 源时钟=%u Hz；按分频规则最高 SCLK=%u Hz", opt->ssi_source_hz, sclk_limit);
        case_result("时钟", "理论上限", 1, detail);
    } else {
        case_result("时钟", "理论上限", 0, "未提供 SSI 源时钟，实际 SCLK 必须由仪器确认");
    }
    for (i = 0; i < sizeof(scan_speeds) / sizeof(scan_speeds[0]) && !stop_requested; i++) {
        unsigned int speed = scan_speeds[i];
        unsigned char *tx;
        unsigned char *rx;
        unsigned int pattern;
        uint64_t bytes = 0;
        struct timespec start;
        struct timespec end;
        double seconds;
        double mbps;
        int ret = 0;
        size_t bad = SIZE_MAX;
        char name[32];
        char detail[260];

        snprintf(name, sizeof(name), "%uHz", speed);
        if (sclk_limit && speed > sclk_limit) {
            snprintf(detail, sizeof(detail), "请求频率超过理论上限 %u Hz", sclk_limit);
            case_result("时钟与带宽", name, 0, detail);
            continue;
        }
        if (configure_spi(ctx, SPI_MODE_0, speed) < 0) {
            case_result("时钟与带宽", name, -1, "SPI 配置或回读失败");
            continue;
        }
        tx = malloc(opt->max_transfer);
        rx = malloc(opt->max_transfer);
        if (!tx || !rx) {
            free(tx);
            free(rx);
            case_result("时钟与带宽", name, -1, "测试缓冲区分配失败");
            continue;
        }
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (pattern = 0; pattern < opt->scan_loops; pattern++) {
            fill_pattern(tx, opt->max_transfer, pattern % 4U, UINT32_C(0x13579bdf) + pattern);
            memset(rx, 0, opt->max_transfer);
            ret = transfer_buffers(ctx, tx, rx, opt->max_transfer, speed, &bad, NULL);
            record_transfer(opt->max_transfer, ret != 0);
            if (ret || add_u64(&bytes, opt->max_transfer) < 0) {
                if (!ret)
                    ret = -EOVERFLOW;
                break;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        seconds = elapsed_seconds(&start, &end);
        mbps = seconds > 0.0 ? (bytes * 8.0 / 1000000.0) / seconds : 0.0;
        if (ret) {
            snprintf(detail,
                     sizeof(detail),
                     "轮数=%u 返回=%d 首个错误偏移=%zu 完成=%" PRIu64 "B 有效带宽=%.2fMbit/s",
                     pattern,
                     ret,
                     bad,
                     bytes,
                     mbps);
            case_result("时钟与带宽", name, -1, detail);
        } else {
            snprintf(detail, sizeof(detail), "图样轮数=%u 完成=%" PRIu64 "B 有效带宽=%.2fMbit/s", opt->scan_loops, bytes, mbps);
            case_result("时钟与带宽", name, 1, detail);
            if (speed >= result.highest_speed) {
                result.highest_speed = speed;
                result.payload_mbps = mbps;
            }
        }
        free(tx);
        free(rx);
    }
    return result;
}

static int configure_hardware_cs_pin(unsigned int bus, const char *gpio)
{
    char *funcs[2];
    int fd;
    int ret;

    if (bus == 0U && !strcasecmp(gpio, "pd05"))
        funcs[0] = "func2";
    else if (bus == 0U && !strcasecmp(gpio, "pb00"))
        funcs[0] = "func0";
    else if (bus == 1U && !strcasecmp(gpio, "pc30"))
        funcs[0] = "func1";
    else if (bus == 1U && !strcasecmp(gpio, "pc18"))
        funcs[0] = "func2";
    else
        return -EINVAL;
    funcs[1] = "pull_hiz";
    fd = gpio_open();
    if (fd < 0)
        return -errno;
    ret = gpio_set_func(fd, gpio, funcs, 2U);
    gpio_close(fd);
    return ret;
}

static void restore_hardware_cs_pin(const char *gpio)
{
    char *funcs[] = { "input", "pull_hiz" };
    int fd = gpio_open();

    if (fd < 0)
        return;
    gpio_set_func(fd, gpio, funcs, 2U);
    gpio_close(fd);
}

static int prompt_uint_measurement(const char *prompt, unsigned int *value)
{
    char line[64];
    size_t len;

    printf("%s（直接回车跳过）: ", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
        return 0;
    len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
    if (!line[0])
        return 0;
    return parse_uint(line, value) == 0 ? 1 : -1;
}

static void
run_instrument_capture(spi_context_t *ctx, const test_options_t *opt, unsigned int speed, const char *cs_label, const char *kind)
{
    size_t len = ctx->spidev_bufsiz < 4096U ? ctx->spidev_bufsiz : 4096U;
    unsigned char *tx;
    char detail[240];
    unsigned int measured;
    int ret;

    tx = malloc(len);
    if (!tx) {
        case_result("CS 时序", kind, -1, "测试缓冲区分配失败");
        return;
    }
    fill_pattern(tx, len, 3U, UINT32_C(0x5a5a5a5a));
    ret = configure_spi(ctx, SPI_MODE_0, speed);
    if (ret == 0)
        ret = raw_transfer(ctx->fd, tx, NULL, 1U, speed, 1U);
    if (ret == 0) {
        printf("[%s] 探头接 CS=%s，按 Enter 后 %u ms 发起 %zuB 采样传输。\n", kind, cs_label, opt->cs_arm_ms, len);
        (void)getchar();
        usleep(opt->cs_arm_ms * 1000U);
        ret = raw_transfer(ctx->fd, tx, NULL, len, speed, 1U);
    }
    snprintf(detail, sizeof(detail), "CS=%s 请求频率=%uHz 单次传输=%zuB", cs_label, speed, len);
    case_result("仪器", kind, ret >= 0 ? 1 : -1, detail);
    if (ret >= 0) {
        if (prompt_uint_measurement("输入仪器测得的 SCLK 频率(Hz)", &measured) > 0) {
            snprintf(detail, sizeof(detail), "实测=%uHz 请求=%uHz", measured, speed);
            case_result("SCLK", kind, measured <= SSI_IO_MAX_HZ ? 1 : -1, detail);
        }
        if (opt->cs_to_clk_ns_valid) {
            snprintf(detail, sizeof(detail), "实测=%uns CS=%s", opt->cs_to_clk_ns, cs_label);
            case_result("CS 到首个 SCLK", kind, 1, detail);
        } else if (prompt_uint_measurement("输入 CS 下降沿到首个 SCLK 的间隔(ns)", &measured) > 0) {
            snprintf(detail, sizeof(detail), "实测=%uns CS=%s", measured, cs_label);
            case_result("CS 到首个 SCLK", kind, 1, detail);
        }
    }
    free(tx);
}

static void run_cs_timing_sweep(spi_context_t *ctx, const test_options_t *opt, const char *cs_label, const char *kind)
{
    static const unsigned int speeds[] = { 1000000U, 10000000U, TEST_SCLK_HZ };
    size_t i;

    report_printf("\n## %s CS 到 SCLK 时序\n\n", kind);
    for (i = 0; i < sizeof(speeds) / sizeof(speeds[0]) && !stop_requested; i++)
        run_instrument_capture(ctx, opt, speeds[i], cs_label, kind);
}

static int run_duplex_window(spi_context_t *ctx,
                             size_t len,
                             double minimum_seconds,
                             uint32_t *seed,
                             uint64_t *bytes,
                             double *seconds,
                             size_t *bad_offset)
{
    unsigned char *tx;
    unsigned char *rx;
    struct timespec start;
    struct timespec end;
    int ret = 0;

    *bytes = 0;
    *seconds = 0.0;
    *bad_offset = SIZE_MAX;
    if (configure_spi(ctx, SPI_MODE_0, TEST_SCLK_HZ) < 0) {
        case_result("压力测试", "SPI 配置", -1, "SPI 配置或回读失败");
        return -EINVAL;
    }
    tx = malloc(len);
    rx = malloc(len);
    if (!tx || !rx) {
        case_result("压力测试", "测试缓冲区", -1, "分配失败");
        free(tx);
        free(rx);
        return -ENOMEM;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        ret = checked_transfer(ctx, tx, rx, len, 4U, (*seed)++, bad_offset);
        if (ret || add_u64(bytes, (uint64_t)len) < 0) {
            if (!ret)
                ret = -EOVERFLOW;
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
    } while (!stop_requested && elapsed_seconds(&start, &end) < minimum_seconds);
    if (ret == 0)
        clock_gettime(CLOCK_MONOTONIC, &end);
    *seconds = elapsed_seconds(&start, &end);
    free(tx);
    free(rx);
    return stop_requested && !ret ? -ECANCELED : ret;
}

static int run_preflight(spi_context_t *ctx, unsigned int bus)
{
    size_t index;
    uint32_t seed = UINT32_C(0x2468ace1) ^ bus;
    const size_t *groups[] = { primary_lengths, boundary_lengths };
    const size_t group_counts[] = {
        sizeof(primary_lengths) / sizeof(primary_lengths[0]),
        sizeof(boundary_lengths) / sizeof(boundary_lengths[0]),
    };
    size_t group;

    report_printf("\n## SPI%u 短时无错预检\n\n", bus);
    for (group = 0; group < sizeof(groups) / sizeof(groups[0]); group++) {
        for (index = 0; index < group_counts[group] && !stop_requested; index++) {
            size_t len = groups[group][index];
            uint64_t bytes;
            double seconds;
            size_t bad;
            int ret;
            char name[48];
            char detail[180];

            snprintf(name, sizeof(name), "%zuB", len);
            ret = run_checked_once(ctx, 1, len, seed++, &bad);
            if (ret) {
                snprintf(detail, sizeof(detail), "内部 LOOP 返回=%d 错误偏移=%zu", ret, bad);
                case_result("预检", name, -1, detail);
                return -1;
            }
            ret = run_duplex_window(ctx, len, 5.0, &seed, &bytes, &seconds, &bad);
            if (ret) {
                snprintf(detail, sizeof(detail), "外部全双工返回=%d 错误偏移=%zu 完成=%" PRIu64 "B", ret, bad, bytes);
                case_result("预检", name, -1, detail);
                return -1;
            }
            snprintf(detail, sizeof(detail), "内部 LOOP 通过；外部全双工 %.3fs 完成=%" PRIu64 "B", seconds, bytes);
            case_result("预检", name, 1, detail);
        }
    }
    return stop_requested ? -1 : 0;
}

static int write_sample_csv(unsigned int bus,
                            const char *phase,
                            size_t len,
                            unsigned int sample,
                            double seconds,
                            uint64_t bytes,
                            double mbps)
{
    if (!report.samples_fp)
        return -1;
    if (fprintf(report.samples_fp,
                "%u,%s,%zu,%u,%.3f,%" PRIu64 ",%.6f\n",
                bus,
                phase,
                len,
                sample,
                seconds * 1000.0,
                bytes,
                mbps) < 0)
        return -1;
    if (fflush(report.samples_fp) != 0) {
        report.output_failed = 1;
        return -1;
    }
    return 0;
}

static int run_sample_set(spi_context_t *ctx,
                          unsigned int bus,
                          const char *phase,
                          size_t len,
                          unsigned int count,
                          double minimum_seconds,
                          uint32_t *seed)
{
    spi_stats_t stats;
    uint64_t total_bytes = 0;
    unsigned int sample;
    char name[64];
    char detail[360];

    spi_stats_init(&stats);
    for (sample = 0; sample < count && !stop_requested; sample++) {
        uint64_t bytes;
        double seconds;
        double mbps;
        size_t bad;
        int ret = run_duplex_window(ctx, len, minimum_seconds, seed, &bytes, &seconds, &bad);

        if (ret || seconds <= 0.0 || add_u64(&total_bytes, bytes) < 0 ||
            spi_stats_add(&stats, (bytes * 8.0 / 1000000.0) / seconds) < 0) {
            snprintf(name, sizeof(name), "%s-%zuB", phase, len);
            snprintf(detail,
                     sizeof(detail),
                     "样本=%u/%u 返回=%d 错误偏移=%zu 完成=%" PRIu64 "B",
                     sample + 1,
                     count,
                     ret,
                     bad,
                     bytes);
            case_result("资格测试", name, -1, detail);
            return -1;
        }
        mbps = spi_stats_mean(&stats);
        if (write_sample_csv(bus, phase, len, sample + 1, seconds, bytes, (bytes * 8.0 / 1000000.0) / seconds) < 0) {
            case_result("资格测试", phase, -1, "写入样本 CSV 失败");
            return -1;
        }
        (void)mbps;
    }
    snprintf(name, sizeof(name), "%s-%zuB", phase, len);
    if (stats.count != count) {
        snprintf(detail, sizeof(detail), "仅完成 %zu/%u 个样本", stats.count, count);
        case_result("资格测试", name, -1, detail);
        return -1;
    }
    snprintf(detail,
             sizeof(detail),
             "样本=%zu 总负载=%" PRIu64 "B 中位数=%.3f 最小=%.3f 最大=%.3f 均值=%.3f 标准差=%.3f Mbit/s",
             stats.count,
             total_bytes,
             spi_stats_median(&stats),
             spi_stats_min(&stats),
             spi_stats_max(&stats),
             spi_stats_mean(&stats),
             spi_stats_sample_stddev(&stats));
    case_result("资格测试", name, 1, detail);
    return 0;
}

static int run_stress(spi_context_t *ctx, unsigned int bus, uint32_t *seed, unsigned int duration_seconds)
{
    unsigned char *tx;
    unsigned char *rx;
    struct timespec start;
    struct timespec end;
    uint64_t bytes = 0;
    uint64_t transfers = 0;
    size_t bad = SIZE_MAX;
    int ret = 0;
    char detail[280];
    double seconds;
    double last_progress_seconds = 0.0;

    if (configure_spi(ctx, SPI_MODE_0, TEST_SCLK_HZ) < 0)
        return -EINVAL;
    tx = malloc(STRESS_LENGTH);
    rx = malloc(STRESS_LENGTH);
    if (!tx || !rx) {
        free(tx);
        free(rx);
        return -ENOMEM;
    }
    report_printf("\n## SPI%u %u 秒随机数据压力\n\n", bus, duration_seconds);
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        ret = checked_transfer(ctx, tx, rx, STRESS_LENGTH, 4U, (*seed)++, &bad);
        if (ret || add_u64(&bytes, STRESS_LENGTH) < 0 || add_u64(&transfers, 1) < 0) {
            if (!ret)
                ret = -EOVERFLOW;
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        seconds = elapsed_seconds(&start, &end);
        if (seconds - last_progress_seconds >= 10.0) {
            printf("[压力测试] SPI%u 已运行 %.1fs，发送=%" PRIu64 "B transfer=%" PRIu64
                   " 有效带宽=%.3fMbit/s\n",
                   bus,
                   seconds,
                   bytes,
                   transfers,
                   seconds > 0.0 ? (bytes * 8.0 / 1000000.0) / seconds : 0.0);
            last_progress_seconds = seconds;
        }
    } while (!stop_requested && seconds < (double)duration_seconds);
    clock_gettime(CLOCK_MONOTONIC, &end);
    seconds = elapsed_seconds(&start, &end);
    free(tx);
    free(rx);
    if (ret || stop_requested) {
        snprintf(detail,
                 sizeof(detail),
                 "返回=%d 错误偏移=%zu 完成=%" PRIu64 "B transfer=%" PRIu64 " 耗时=%.3fs",
                 ret,
                 bad,
                 bytes,
                 transfers,
                 seconds);
        case_result("压力测试", "外部全双工随机数据", -1, detail);
        return -1;
    }
    snprintf(detail,
             sizeof(detail),
             "完成=%" PRIu64 "B transfer=%" PRIu64 " 耗时=%.3fs 有效带宽=%.3fMbit/s 数据错误=0",
             bytes,
             transfers,
             seconds,
             seconds > 0.0 ? (bytes * 8.0 / 1000000.0) / seconds : 0.0);
    case_result("压力测试", "外部全双工随机数据", 1, detail);
    return 0;
}

static int run_stress_mode(const test_options_t *opt, size_t spidev_bufsiz)
{
    spi_context_t ctx;
    uint32_t seed = UINT32_C(0x9e3779b9) ^ opt->bus;
    int ret;

    if (spi_context_init(&ctx, opt->bus, opt->cs, spidev_bufsiz) < 0) {
        case_result("SPI 初始化", opt->cs, -1, "注册或打开 spidev 失败");
        return -1;
    }
    case_result("SPI 初始化", opt->cs, 1, ctx.path);
    print_driver_parameters(opt->bus);
    ret = run_stress(&ctx, opt->bus, &seed, opt->stress_seconds);
    spi_context_deinit(&ctx);
    return ret;
}

static int run_qualification_bus(const qualification_profile_t *profile, const test_options_t *opt, size_t spidev_bufsiz)
{
    spi_context_t ctx;
    scan_result_t scan;
    uint32_t seed = UINT32_C(0x9e3779b9) ^ profile->bus;
    size_t i;
    int failed = 0;
    char detail[220];

    report_printf("\n# SPI%u 资格测试\n\n- 引脚: CLK=%s MOSI=%s MISO=%s CS=%s\n",
                  profile->bus,
                  profile->clk,
                  profile->mosi,
                  profile->miso,
                  profile->cs);
    if (spi_context_init(&ctx, profile->bus, profile->cs, spidev_bufsiz) < 0) {
        case_result("SPI 初始化", profile->cs, -1, "注册或打开 spidev 失败");
        return -1;
    }
    case_result("SPI 初始化", profile->cs, 1, ctx.path);
    print_driver_parameters(profile->bus);
    scan = run_frequency_scan(&ctx, opt);
    if (scan.highest_speed != TEST_SCLK_HZ) {
        snprintf(detail, sizeof(detail), "最高无错档=%uHz，资格测试要求 60000000Hz", scan.highest_speed);
        case_result("资格前提", "60MHz", -1, detail);
        failed = 1;
    } else if (run_preflight(&ctx, profile->bus) < 0) {
        case_result("资格前提", "短时无错预检", -1, "预检失败，跳过该 SPI 长测");
        failed = 1;
    }
    if (!failed) {
        report_printf("\n## SPI%u 主长度带宽统计\n\n", profile->bus);
        for (i = 0; i < sizeof(primary_lengths) / sizeof(primary_lengths[0]); i++) {
            if (run_sample_set(&ctx, profile->bus, "primary", primary_lengths[i], 1000U, 2.5, &seed) < 0) {
                failed = 1;
                break;
            }
        }
    }
    if (!failed) {
        report_printf("\n## SPI%u 边界带宽统计\n\n", profile->bus);
        for (i = 0; i < sizeof(boundary_lengths) / sizeof(boundary_lengths[0]); i++) {
            if (run_sample_set(&ctx, profile->bus, "boundary", boundary_lengths[i], 60U, 5.0, &seed) < 0) {
                failed = 1;
                break;
            }
        }
    }
    if (!failed && run_stress(&ctx, profile->bus, &seed, SPI_STRESS_DEFAULT_SECONDS) < 0)
        failed = 1;
    spi_context_deinit(&ctx);
    return failed ? -1 : 0;
}

static int run_qualification(const test_options_t *opt, size_t spidev_bufsiz)
{
    size_t i;
    int failed = 0;

    report_printf("\n## 资格测试参数\n\n- 主长度: 16KiB、128KiB、1MiB；每档 1000 "
                  "样本，每样本至少 2.5 秒\n"
                  "- 边界: 127/128/129/4095/4096/4097B；每档 60 样本，每样本至少 5 秒\n"
                  "- 压力: 64KiB 外部全双工随机数据 5400 秒\n"
                  "- 原始样本 CSV: `%s`\n",
                  report.samples_path);
    for (i = 0; i < sizeof(qualification_profiles) / sizeof(qualification_profiles[0]) && !stop_requested; i++) {
        if (run_qualification_bus(&qualification_profiles[i], opt, spidev_bufsiz) < 0)
            failed = 1;
    }
    return failed || stop_requested ? -1 : 0;
}

static int run_complete(const test_options_t *opt, size_t spidev_bufsiz)
{
    spi_context_t ctx;
    scan_result_t scan;
    char detail[160];

    if (spi_context_init(&ctx, opt->bus, opt->cs, spidev_bufsiz) < 0) {
        case_result("SPI 初始化", opt->cs, -1, "注册或打开 spidev 失败");
        return -1;
    }
    case_result("SPI 初始化", opt->cs, 1, ctx.path);
    print_driver_parameters(opt->bus);
    scan = run_frequency_scan(&ctx, opt);
    snprintf(detail, sizeof(detail), "最高无错请求频率=%uHz 有效带宽=%.3fMbit/s", scan.highest_speed, scan.payload_mbps);
    case_result("快速完整测试", "最高无错档", scan.highest_speed ? 1 : -1, detail);
    spi_context_deinit(&ctx);
    return scan.highest_speed ? 0 : -1;
}

static int run_max_sclk(const test_options_t *opt, size_t spidev_bufsiz)
{
    spi_clock_plan_t plan;
    spi_context_t ctx;
    unsigned char *tx = NULL;
    unsigned char *rx = NULL;
    struct timespec start;
    struct timespec end;
    uint64_t bytes = 0;
    unsigned int speed;
    unsigned int pattern;
    size_t bad = SIZE_MAX;
    spi_timing_t timing;
    double seconds;
    double mbps;
    double non_data_seconds;
    int ret = 0;
    char detail[300];

    if (spi_clock_plan_from_mpll(opt->div_ssi_rate, &plan) < 0 || plan.actual_sclk_hz > UINT_MAX) {
        case_result("理论时钟", "CGV=0", -1, "div_ssi_rate 无法由 MPLL 整数分频表示");
        return -1;
    }
    speed = (unsigned int)plan.actual_sclk_hz;
    snprintf(detail,
             sizeof(detail),
             "MPLL=%" PRIu64 "Hz 输入 div_ssi_rate=%" PRIu64 "Hz SSICDR 分频=%u "
             "理论 div_ssi=%" PRIu64 "Hz CGV=0 理论 SCLK=%uHz",
             SPI_MPLL_HZ,
             plan.requested_div_ssi_hz,
             plan.ssicdr_divisor,
             plan.actual_div_ssi_hz,
             speed);
    printf("[理论时钟] %s\n", detail);
    case_result("理论时钟", "CGV=0", 1, detail);
    if (spi_context_init(&ctx, opt->bus, opt->cs, spidev_bufsiz) < 0) {
        case_result("SPI 初始化", opt->cs, -1, "注册或打开 spidev 失败");
        return -1;
    }
    case_result("SPI 初始化", opt->cs, 1, ctx.path);
    print_driver_parameters(opt->bus);
    if (configure_spi(&ctx, SPI_MODE_0, speed) < 0) {
        case_result("CGV=0 单档测试", "SPI 配置", -1, "SPI 配置或回读失败");
        spi_context_deinit(&ctx);
        return -1;
    }
    tx = malloc(opt->max_transfer);
    rx = malloc(opt->max_transfer);
    if (!tx || !rx) {
        case_result("CGV=0 单档测试", "测试缓冲区", -1, "分配失败");
        free(tx);
        free(rx);
        spi_context_deinit(&ctx);
        return -1;
    }
    spi_timing_init(&timing);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (pattern = 0; pattern < opt->scan_loops; pattern++) {
        fill_pattern(tx, opt->max_transfer, pattern % 4U, UINT32_C(0x13579bdf) + pattern);
        memset(rx, 0, opt->max_transfer);
        ret = transfer_buffers(&ctx, tx, rx, opt->max_transfer, speed, &bad, &timing);
        record_transfer(opt->max_transfer, ret != 0);
        if (ret || add_u64(&bytes, opt->max_transfer) < 0) {
            if (!ret)
                ret = -EOVERFLOW;
            break;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    seconds = elapsed_seconds(&start, &end);
    mbps = seconds > 0.0 ? (bytes * 8.0 / 1000000.0) / seconds : 0.0;
    non_data_seconds = spi_timing_non_data_seconds(&timing);
    if (ret) {
        snprintf(detail,
                 sizeof(detail),
                 "理论 SCLK=%uHz 轮数=%u 返回=%d 首个错误偏移=%zu 完成=%" PRIu64
                 "B 有效带宽=%.2fMbit/s",
                 speed,
                 pattern,
                 ret,
                 bad,
                 bytes,
                 mbps);
        case_result("CGV=0 单档测试", "外部全双工", -1, detail);
    } else {
        snprintf(detail,
                 sizeof(detail),
                 "理论 SCLK=%uHz 图样轮数=%u 完成=%" PRIu64 "B 有效带宽=%.2fMbit/s",
                 speed,
                 opt->scan_loops,
                 bytes,
                 mbps);
        case_result("CGV=0 单档测试", "外部全双工", 1, detail);
    }
    snprintf(detail,
             sizeof(detail),
             "ioctl 次数=%" PRIu64 " 每次理论数据周期=%.3fus 每次实测 ioctl 周期=%.3fus",
             timing.ioctl_count,
             spi_timing_average_theoretical_seconds(&timing) * 1000000.0,
             spi_timing_average_ioctl_seconds(&timing) * 1000000.0);
    printf("[传输时序] %s\n", detail);
    case_result("传输时序", "单次平均周期", timing.ioctl_count ? 1 : -1, detail);
    snprintf(detail,
             sizeof(detail),
             "理论数据总时间=%.6fs 实测 ioctl 总时间=%.6fs ioctl 数据占空比=%.2f%%",
             timing.theoretical_seconds,
             timing.ioctl_seconds,
             spi_timing_ioctl_duty_percent(&timing));
    printf("[传输时序] %s\n", detail);
    case_result("传输时序", "ioctl 数据时间", timing.ioctl_count ? 1 : -1, detail);
    if (non_data_seconds >= 0.0) {
        snprintf(detail,
                 sizeof(detail),
                 "ioctl 内非数据时间=%.6fs，平均每次=%.3fus（含系统调用、驱动、调度和 CS 间隙）",
                 non_data_seconds,
                 timing.ioctl_count ? non_data_seconds * 1000000.0 / (double)timing.ioctl_count : 0.0);
        printf("[传输时序] %s\n", detail);
        case_result("传输时序", "ioctl 内非数据时间", timing.ioctl_count ? 1 : -1, detail);
    } else {
        snprintf(detail,
                 sizeof(detail),
                 "实测 ioctl 时间比理论数据时间少 %.6fs，不能将该差值解释为开销",
                 -non_data_seconds);
        printf("[传输时序] %s\n", detail);
        case_result("传输时序", "ioctl 内非数据时间", 0, detail);
    }
    snprintf(detail,
             sizeof(detail),
             "整轮实测总时间=%.6fs 理论数据时间占整轮=%.2f%%",
             seconds,
             spi_timing_wall_duty_percent(&timing, seconds));
    printf("[传输时序] %s\n", detail);
    case_result("传输时序", "整轮数据占空比", timing.ioctl_count ? 1 : -1, detail);
    free(tx);
    free(rx);
    spi_context_deinit(&ctx);
    return ret ? -1 : 0;
}

static int run_always_speed(const test_options_t *opt, size_t spidev_bufsiz)
{
    spi_context_t ctx;
    unsigned char *tx = NULL;
    struct timespec start;
    struct timespec last_print;
    struct timespec now;
    uint64_t bytes = 0;
    uint64_t transfers = 0;
    double seconds;
    int ret = 0;
    char detail[240];

    if (spi_context_init(&ctx, opt->bus, opt->cs, spidev_bufsiz) < 0) {
        case_result("连续发送", opt->cs, -1, "注册或打开 spidev 失败");
        return -1;
    }
    if (configure_spi(&ctx, SPI_MODE_0, opt->always_speed) < 0) {
        case_result("连续发送", "SPI 配置", -1, "SPI 配置或回读失败");
        spi_context_deinit(&ctx);
        return -1;
    }
    tx = malloc(ctx.spidev_bufsiz);
    if (!tx) {
        case_result("连续发送", "发送缓冲区", -1, "分配失败");
        spi_context_deinit(&ctx);
        return -1;
    }
    memset(tx, 0x55, ctx.spidev_bufsiz);
    print_driver_parameters(opt->bus);
    printf("[always-speed] SPI%u 请求=%uHz 单条 message=%zuB MOSI=01010101；"
           "按 Ctrl-C 停止。\n",
           opt->bus,
           opt->always_speed,
           ctx.spidev_bufsiz);
    clock_gettime(CLOCK_MONOTONIC, &start);
    last_print = start;
    while (!stop_requested) {
        ret = raw_transfer(ctx.fd, tx, NULL, ctx.spidev_bufsiz, opt->always_speed, 0U);
        if (ret < 0 || add_u64(&bytes, ctx.spidev_bufsiz) < 0 || add_u64(&transfers, 1) < 0) {
            if (ret >= 0)
                ret = -EOVERFLOW;
            break;
        }
        record_transfer(ctx.spidev_bufsiz, 0);
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_seconds(&last_print, &now) >= 2.0) {
            seconds = elapsed_seconds(&start, &now);
            printf("[always-speed] %.1fs transfer=%" PRIu64 " 发送=%" PRIu64 "B 有效带宽=%.3fMbit/s\n",
                   seconds,
                   transfers,
                   bytes,
                   seconds > 0.0 ? (bytes * 8.0 / 1000000.0) / seconds : 0.0);
            last_print = now;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    seconds = elapsed_seconds(&start, &now);
    if (ret < 0) {
        snprintf(detail,
                 sizeof(detail),
                 "返回=%d 完成=%" PRIu64 "B transfer=%" PRIu64 " 耗时=%.3fs",
                 ret,
                 bytes,
                 transfers,
                 seconds);
        case_result("连续发送", "0x55", -1, detail);
    } else {
        snprintf(detail,
                 sizeof(detail),
                 "完成=%" PRIu64 "B transfer=%" PRIu64 " 耗时=%.3fs 有效带宽=%.3fMbit/s",
                 bytes,
                 transfers,
                 seconds,
                 seconds > 0.0 ? (bytes * 8.0 / 1000000.0) / seconds : 0.0);
        case_result("连续发送", "0x55", 1, detail);
        printf("[always-speed] 已停止: %s\n", detail);
    }
    free(tx);
    spi_context_deinit(&ctx);
    return ret < 0 ? -1 : 0;
}

static int run_delay(const test_options_t *opt, size_t spidev_bufsiz)
{
    int run_software = !opt->hw_cs_specified || opt->cs_specified;
    int run_hardware = opt->hw_cs_specified;
    int failed = 0;

    if (run_software) {
        spi_context_t ctx;

        if (spi_context_init(&ctx, opt->bus, opt->cs, spidev_bufsiz) < 0) {
            case_result("软件 CS", opt->cs, -1, "注册或打开 spidev 失败");
            failed = 1;
        } else {
            run_cs_timing_sweep(&ctx, opt, opt->cs, "软件 GPIO CS");
            spi_context_deinit(&ctx);
        }
    }
    if (run_hardware) {
        spi_context_t ctx;

        if (configure_hardware_cs_pin(opt->bus, opt->hw_cs) < 0) {
            case_result("硬件 CE0", opt->hw_cs, -1, "引脚复用失败");
            failed = 1;
        } else if (spi_context_init(&ctx, opt->bus, "-1", spidev_bufsiz) < 0) {
            case_result("硬件 CE0", opt->hw_cs, -1, "使用硬件 CE0 注册 spidev 失败");
            restore_hardware_cs_pin(opt->hw_cs);
            failed = 1;
        } else {
            run_cs_timing_sweep(&ctx, opt, opt->hw_cs, "硬件 SSI_CE0");
            spi_context_deinit(&ctx);
            restore_hardware_cs_pin(opt->hw_cs);
        }
    }
    return failed ? -1 : 0;
}

int main(int argc, char **argv)
{
    test_options_t opt;
    size_t spidev_bufsiz;
    int parse_ret;
    int ret;

    parse_ret = parse_args(argc, argv, &opt);
    if (parse_ret != 0) {
        print_usage(argv[0]);
        return parse_ret > 0 ? 0 : 2;
    }
    if (report_open(&opt) < 0)
        return 2;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    spidev_bufsiz = detect_spidev_bufsiz();
    report_printf("\n## 参数\n\n- spidev 缓冲区: %zuB\n", spidev_bufsiz);
    if (opt.qualification)
        ret = run_qualification(&opt, spidev_bufsiz);
    else if (opt.stress)
        ret = run_stress_mode(&opt, spidev_bufsiz);
    else if (opt.mode == TEST_MODE_DELAY)
        ret = run_delay(&opt, spidev_bufsiz);
    else if (opt.mode == TEST_MODE_ALWAYS_SPEED)
        ret = run_always_speed(&opt, spidev_bufsiz);
    else if (opt.mode == TEST_MODE_MAX_SCLK)
        ret = run_max_sclk(&opt, spidev_bufsiz);
    else
        ret = run_complete(&opt, spidev_bufsiz);
    if (report_close() < 0)
        ret = -1;
    printf("SPI 测试完成: 通过=%" PRIu64 " 失败=%" PRIu64 " 跳过=%" PRIu64 " 传输=%" PRIu64 " 错误=%" PRIu64 "\n报告: %s\n",
           report.pass,
           report.fail,
           report.skip,
           report.transfers,
           report.errors,
           report.path[0] ? report.path : "(none)");
    return ret || report.fail || report.output_failed ? 1 : 0;
}
