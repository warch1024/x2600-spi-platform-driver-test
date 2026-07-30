/*
 * x2600_510 spi.c loopback/pressure test.
 *
 * External tests require the selected SPI bus MOSI and MISO to be physically
 * shorted. The 48MHz performance matrix also includes an internal SPI_LOOP
 * control-path measurement.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
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

#define MAX_CS 8
#define DEFAULT_BUS 1
#define DEFAULT_CS "pc30"
#define DEFAULT_HW_CS "pc30"
#define DEFAULT_SPEED 1000000U
#define DEFAULT_STRESS_SECONDS 60U
#define DEFAULT_STRESS_LEN (64U * 1024U)
#define DEFAULT_MAX_TRANSFER (1024U * 1024U)
#define DEFAULT_CASE_LOOPS 2U
#define DEFAULT_PERF_LOOPS 4U
#define DEFAULT_CS_ARM_MS 2000U
#define DEFAULT_PAYLOAD_LOOPS 4U
#define MAX_TIMING_SPEEDS 3
#define SSI_IO_MAX_HZ 80000000U
#define DEFAULT_MAX_PROBE_HZ SSI_IO_MAX_HZ
#define MAX_SPEEDS 16
#define DEVICE_WAIT_US 20000U
#define DEVICE_WAIT_RETRIES 150U

typedef enum {
  TEST_MODE_INTERNAL,
  TEST_MODE_EXTERNAL,
  TEST_MODE_INSTRUMENT,
  TEST_MODE_COMPLETE,
  TEST_MODE_DELAY,
  TEST_MODE_FREQUENCY,
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
  char cs_text[128];
  unsigned int speeds[MAX_SPEEDS];
  size_t speed_count;
  unsigned int max_transfer;
  unsigned int performance_loops;
  unsigned int cs_arm_ms;
  unsigned int cs_to_clk_ns;
  int cs_to_clk_ns_valid;
  char hw_cs_text[16];
  unsigned int payload_loops;
  int hardware_cs_enabled;
  int soft_cs_specified;
  int hw_cs_specified;
  unsigned int frequency_output_hz;
  unsigned int ssi_source_hz;
  unsigned int max_probe_hz;
  unsigned int stress_seconds;
  unsigned int stress_len;
  unsigned int stress_bpw;
  unsigned int case_loops;
  test_mode_t mode;
  char driver_mode[32];
  int quick;
  const char *report_name;
} test_options_t;

typedef struct {
  FILE *fp;
  char path[PATH_MAX];
  unsigned long pass;
  unsigned long fail;
  unsigned long skip;
  unsigned long transfers;
  unsigned long long bytes;
  unsigned long long errors;
} test_report_t;

typedef struct {
  unsigned int highest_passing_speed;
  double payload_mbps;
} performance_result_t;

static volatile sig_atomic_t stop_requested;
static test_report_t report;

/**
 * 功能: 累计一次传输的次数、字节数和数据错误统计。
 * 参数: len - 本次字节数；bpw - 每字位数；data_errors - 本次数据错误数量。
 * 返回值: 无。
 */
static void record_transfer(size_t len, unsigned int bpw, int data_errors);

/**
 * 功能: 返回测试模式的中文名称。
 * 参数: mode - 待转换的测试模式枚举值。
 * 返回值: 模式名称；未知枚举值返回“未知”。
 */
static const char *test_mode_name(test_mode_t mode) {
  switch (mode) {
  case TEST_MODE_INTERNAL:
    return "内部回环";
  case TEST_MODE_EXTERNAL:
    return "外部回环";
  case TEST_MODE_INSTRUMENT:
    return "仪器测量";
  case TEST_MODE_COMPLETE:
    return "完整测试";
  case TEST_MODE_DELAY:
    return "CS 时序测量";
  case TEST_MODE_FREQUENCY:
    return "连续频率输出";
  }
  return "未知";
}

/**
 * 功能: 返回测试模式的中文说明。
 * 参数: mode - 待说明的测试模式枚举值。
 * 返回值: 模式说明；未知枚举值返回“未知”。
 */
static const char *test_mode_description(test_mode_t mode) {
  switch (mode) {
  case TEST_MODE_INTERNAL:
    return "SSI 控制器内部回环，不从引脚输出有效的 CS、CLK、MOSI 或 MISO 波形";
  case TEST_MODE_EXTERNAL:
    return "普通 SSI 模式外部回环，验证实际 MOSI 到 MISO 数据链路";
  case TEST_MODE_INSTRUMENT:
    return "普通 SSI 模式仅发送，用于示波器或逻辑分析仪捕获";
  case TEST_MODE_COMPLETE:
    return "外部回环、48MHz 数据性能、软件/硬件 CS 仪器测量";
  case TEST_MODE_DELAY:
    return "仅测量指定软件 CS 或硬件 CE0 在 1MHz、10MHz、48MHz 的时序";
  case TEST_MODE_FREQUENCY:
    return "普通 SPI 模式持续发送 01010101，供仪器观察 MOSI 与 SCLK";
  }
  return "未知";
}

/**
 * 功能: 按 SSI 分频规则计算不超过 I/O 规格的最高 SCLK。
 * 参数: ssi_source_hz - 实际 SSI 源时钟频率，单位 Hz。
 * 返回值: 可请求的最高 SCLK，源时钟为 0 时返回 0。
 */
static unsigned int max_supported_sclk(unsigned int ssi_source_hz) {
  unsigned int divider;

  if (!ssi_source_hz)
    return 0;
  /* SCLK = source / (2 * (CGV + 1)); choose the fastest legal divider. */
  divider = (ssi_source_hz + 2 * SSI_IO_MAX_HZ - 1) / (2 * SSI_IO_MAX_HZ);
  return ssi_source_hz / (2 * divider);
}

/**
 * 功能: 读取 spidev 驱动的单次传输缓冲区上限。
 * 参数: 无。
 * 返回值: 缓冲区大小；无法读取或值非法时返回默认 4096 字节。
 */
static size_t detect_spidev_bufsiz(void) {
  FILE *fp;
  unsigned long value;

  fp = fopen("/sys/module/spidev/parameters/bufsiz", "r");
  if (!fp)
    return 4096;
  if (fscanf(fp, "%lu", &value) != 1 || value == 0 || value > SIZE_MAX) {
    fclose(fp);
    return 4096;
  }
  fclose(fp);
  return (size_t)value;
}

/**
 * 功能: 记录终止请求，使长时间测试在当前安全点退出。
 * 参数: signo - 收到的信号编号，当前不区分信号类型。
 * 返回值: 无。
 */
static void on_signal(int signo) {
  (void)signo;
  stop_requested = 1;
}

/**
 * 功能: 计算两个时间点之间的毫秒间隔。
 * 参数: start - 起始时间；end - 结束时间。
 * 返回值: end 相对 start 的经过时间，单位毫秒。
 */
static double elapsed_ms(const struct timespec *start,
                         const struct timespec *end) {
  return (end->tv_sec - start->tv_sec) * 1000.0 +
         (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

/**
 * 功能: 将可变参数格式化内容写入测试报告。
 * 参数: fmt - printf 格式字符串；ap - 与格式字符串对应的参数列表。
 * 返回值: 无；报告未打开时不执行写入。
 */
static void report_vprintf(const char *fmt, va_list ap) {
  if (report.fp) {
    vfprintf(report.fp, fmt, ap);
    fflush(report.fp);
  }
}

/**
 * 功能: 将格式化内容写入测试报告。
 * 参数: fmt - printf 格式字符串；... - 与格式字符串对应的可变参数。
 * 返回值: 无；报告未打开时不执行写入。
 */
static void report_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  report_vprintf(fmt, ap);
  va_end(ap);
}

/**
 * 功能: 创建报告文件并写入测试环境摘要。
 * 参数: opt - 已解析的测试选项；driver_path - 被测驱动源码路径说明。
 * 返回值: 成功返回 0，创建或写入初始化失败返回 -1。
 */
static int report_open(const test_options_t *opt, const char *driver_path) {
  char path[PATH_MAX];
  struct timespec ts;
  struct tm tmv;
  time_t wall;

  if (opt->report_name) {
    snprintf(path, sizeof(path), "%s", opt->report_name);
  } else {
    wall = time(NULL);
    localtime_r(&wall, &tmv);
    snprintf(path, sizeof(path),
             "/tmp/spi_test_report_%04d%02d%02d_%02d%02d%02d.md",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
             tmv.tm_min, tmv.tm_sec);
  }

  report.fp = fopen(path, "w");
  if (!report.fp) {
    fprintf(stderr, "无法创建报告 %s: %s\n", path, strerror(errno));
    return -1;
  }
  snprintf(report.path, sizeof(report.path), "%s", path);
  clock_gettime(CLOCK_REALTIME, &ts);
  report_printf("# X2600 SPI 性能测试报告\n\n");
  report_printf("- 生成时间: %s", ctime(&ts.tv_sec));
  report_printf("- 驱动源码: `%s`\n", driver_path);
  report_printf("- 测试模式: `%s` - %s\n", test_mode_name(opt->mode),
                test_mode_description(opt->mode));
  if (opt->mode == TEST_MODE_EXTERNAL || opt->mode == TEST_MODE_COMPLETE)
    report_printf("- 电气条件: 将当前 SPI 总线的 `spiN_mosi` 与 `spiN_miso` "
                  "指定引脚在 SoC 侧外部短接。\n");
  report_printf("- 测试目标: 最大时钟、48MHz 数据路径有效带宽、软件/硬件 CS "
                "下降沿到首个 SCLK 的仪器测量\n");
  report_printf(
      "- 说明: 用户态不能读取纳秒级 GPIO/SCLK 时序；仪器测量阶段会在当前 "
      "`spiN_clk` 引脚和 CS 上产生可供示波器或逻辑分析仪测量的波形。\n\n");
  return 0;
}

/**
 * 功能: 写入汇总结果并关闭测试报告。
 * 参数: 无。
 * 返回值: 无；报告未打开时直接返回。
 */
static void report_close(void) {
  if (!report.fp)
    return;
  report_printf("\n## 汇总\n\n");
  report_printf("- 通过: %lu\n- 失败: %lu\n- 跳过: %lu\n", report.pass,
                report.fail, report.skip);
  report_printf("- transfer 次数: %lu\n- 检查字节: %llu\n- 数据错误: %llu\n",
                report.transfers, report.bytes, report.errors);
  fclose(report.fp);
  report.fp = NULL;
}

/**
 * 功能: 记录一个测试用例的结果，并更新全局统计信息。
 * 参数: category - 用例分类；name - 用例名称；status - 正数通过、负数失败、0
 * 跳过；detail - 结果详情。 返回值: 无。
 */
static void case_result(const char *category, const char *name, int status,
                        const char *detail) {
  const char *word = status > 0 ? "通过" : status < 0 ? "失败" : "跳过";
  if (status > 0)
    report.pass++;
  else if (status < 0)
    report.fail++;
  else
    report.skip++;
  if (status <= 0)
    printf("[%s] %s/%s%s%s\n", word, category, name,
           detail && detail[0] ? " - " : "", detail && detail[0] ? detail : "");
  report_printf("| %s | %s | %s | %s |\n", word, category, name,
                detail && detail[0] ? detail : "");
}

/**
 * 功能: 将完整的数字文本解析为无符号整数。
 * 参数: text - 待解析文本；value - 成功时写入解析结果。
 * 返回值: 成功返回 0，格式、范围或空值错误返回 -1。
 */
static int parse_uint(const char *text, unsigned int *value) {
  char *end;
  unsigned long v;

  errno = 0;
  v = strtoul(text, &end, 0);
  if (errno || end == text || *end || v > UINT_MAX)
    return -1;
  *value = (unsigned int)v;
  return 0;
}

/**
 * 功能: 解析逗号分隔的 SCLK 频率列表。
 * 参数: text - 频率列表文本；opt - 写入 speeds 和 speed_count 的测试选项。
 * 返回值: 成功返回 0，列表为空、数值非法或超过上限时返回 -1。
 */
static int parse_speeds(const char *text, test_options_t *opt) {
  char copy[256];
  char *save = NULL;
  char *token;
  unsigned int value;

  if (strlen(text) >= sizeof(copy))
    return -1;
  strcpy(copy, text);
  token = strtok_r(copy, ",", &save);
  while (token) {
    if (opt->speed_count >= MAX_SPEEDS || parse_uint(token, &value) < 0 ||
        value == 0)
      return -1;
    opt->speeds[opt->speed_count++] = value;
    token = strtok_r(NULL, ",", &save);
  }
  return opt->speed_count ? 0 : -1;
}

/**
 * 功能: 将命令行模式名称转换为测试模式枚举。
 * 参数: text - 模式名称；mode - 成功时写入对应枚举值。
 * 返回值: 成功返回 0，不支持的模式返回 -1。
 */
static int parse_test_mode(const char *text, test_mode_t *mode) {
  if (!strcmp(text, "internal"))
    *mode = TEST_MODE_INTERNAL;
  else if (!strcmp(text, "external"))
    *mode = TEST_MODE_EXTERNAL;
  else if (!strcmp(text, "instrument"))
    *mode = TEST_MODE_INSTRUMENT;
  else if (!strcmp(text, "complete"))
    *mode = TEST_MODE_COMPLETE;
  else if (!strcmp(text, "delay"))
    *mode = TEST_MODE_DELAY;
  else
    return -1;
  return 0;
}

/**
 * 功能: 解析命令行参数并填充测试选项默认值。
 * 参数: argc - 参数数量；argv - 参数数组；opt - 输出的测试选项。
 * 返回值: 成功返回 0，请求帮助返回 1，参数非法返回 -1。
 */
static int parse_args(int argc, char **argv, test_options_t *opt) {
  int i;
  unsigned int value;

  memset(opt, 0, sizeof(*opt));
  opt->bus = DEFAULT_BUS;
  snprintf(opt->cs_text, sizeof(opt->cs_text), "%s", DEFAULT_CS);
  opt->max_transfer = DEFAULT_MAX_TRANSFER;
  opt->performance_loops = DEFAULT_PERF_LOOPS;
  opt->cs_arm_ms = DEFAULT_CS_ARM_MS;
  opt->payload_loops = DEFAULT_PAYLOAD_LOOPS;
  opt->hardware_cs_enabled = 1;
  snprintf(opt->hw_cs_text, sizeof(opt->hw_cs_text), "%s", DEFAULT_HW_CS);
  opt->mode = TEST_MODE_COMPLETE;
  opt->max_probe_hz = DEFAULT_MAX_PROBE_HZ;
  snprintf(opt->driver_mode, sizeof(opt->driver_mode), "not-specified");
  if (parse_speeds(
          "1000000,5000000,10000000,20000000,25000000,50000000,80000000", opt) <
      0)
    return -1;

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
      return 1;
    if (!strcmp(argv[i], "--quick")) {
      opt->quick = 1;
      continue;
    }
    if (!strcmp(argv[i], "--no-hw-cs")) {
      opt->hardware_cs_enabled = 0;
      continue;
    }
    if (i + 1 >= argc)
      return -1;
    if (!strcmp(argv[i], "--mode")) {
      if (parse_test_mode(argv[++i], &opt->mode) < 0)
        return -1;
    } else if (!strcmp(argv[i], "--bus")) {
      if (parse_uint(argv[++i], &opt->bus) < 0)
        return -1;
    } else if (!strcmp(argv[i], "--cs")) {
      if (strlen(argv[++i]) >= sizeof(opt->cs_text))
        return -1;
      snprintf(opt->cs_text, sizeof(opt->cs_text), "%s", argv[i]);
      opt->soft_cs_specified = 1;
    } else if (!strcmp(argv[i], "--hw-cs")) {
      if (strlen(argv[++i]) >= sizeof(opt->hw_cs_text))
        return -1;
      snprintf(opt->hw_cs_text, sizeof(opt->hw_cs_text), "%s", argv[i]);
      opt->hw_cs_specified = 1;
    } else if (!strcmp(argv[i], "--speeds")) {
      opt->speed_count = 0;
      if (parse_speeds(argv[++i], opt) < 0)
        return -1;
    } else if (!strcmp(argv[i], "--max-transfer")) {
      if (parse_uint(argv[++i], &opt->max_transfer) < 0 || !opt->max_transfer)
        return -1;
    } else if (!strcmp(argv[i], "--loops")) {
      if (parse_uint(argv[++i], &opt->performance_loops) < 0 ||
          !opt->performance_loops)
        return -1;
    } else if (!strcmp(argv[i], "--payload-loops")) {
      if (parse_uint(argv[++i], &opt->payload_loops) < 0 || !opt->payload_loops)
        return -1;
    } else if (!strcmp(argv[i], "--freq")) {
      if (parse_uint(argv[++i], &opt->frequency_output_hz) < 0 ||
          !opt->frequency_output_hz)
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
    } else if (!strcmp(argv[i], "--max-probe-hz")) {
      if (parse_uint(argv[++i], &opt->max_probe_hz) < 0 || !opt->max_probe_hz ||
          opt->max_probe_hz > SSI_IO_MAX_HZ)
        return -1;
    } else if (!strcmp(argv[i], "--stress-seconds")) {
      if (parse_uint(argv[++i], &opt->stress_seconds) < 0)
        return -1;
    } else if (!strcmp(argv[i], "--stress-len")) {
      if (parse_uint(argv[++i], &opt->stress_len) < 0 || !opt->stress_len)
        return -1;
    } else if (!strcmp(argv[i], "--stress-bpw")) {
      if (parse_uint(argv[++i], &opt->stress_bpw) < 0 || opt->stress_bpw < 2 ||
          opt->stress_bpw > 32)
        return -1;
    } else if (!strcmp(argv[i], "--case-loops")) {
      if (parse_uint(argv[++i], &opt->case_loops) < 0 || !opt->case_loops)
        return -1;
    } else if (!strcmp(argv[i], "--driver-mode")) {
      if (strlen(argv[++i]) >= sizeof(opt->driver_mode))
        return -1;
      snprintf(opt->driver_mode, sizeof(opt->driver_mode), "%s", argv[i]);
    } else if (!strcmp(argv[i], "--report")) {
      opt->report_name = argv[++i];
    } else {
      return -1;
    }
  }
  if (opt->quick && opt->max_transfer > 65536U)
    opt->max_transfer = 65536U;
  if (opt->frequency_output_hz)
    opt->mode = TEST_MODE_FREQUENCY;
  value = opt->max_transfer;
  if (value < 8)
    return -1;
  return 0;
}

/**
 * 功能: 输出命令行用法和所有支持选项。
 * 参数: name - 可执行文件名称。
 * 返回值: 无。
 */
static void print_usage(const char *name) {
  printf("用法: %s [选项]\n", name);
  printf("  --mode MODE             "
         "complete(默认)、internal、external、instrument 或 delay\n");
  printf("                          internal: 芯片内部回环；external: 外部 "
         "MOSI-MISO 回环；\n");
  printf("                          instrument: 单次仪器测量；complete: "
         "全部性能和双 CS 时序测量；\n");
  printf("                          delay: --cs 测软件 CS，--hw-cs 测硬件 "
         "CE0，均为 1/10/48MHz\n");
  printf("  --cs GPIO               软件 CS GPIO，默认 %s\n", DEFAULT_CS);
  printf("  --hw-cs GPIO            硬件 CE0 GPIO，默认 %s（SPI0 CE0 功能）\n",
         DEFAULT_HW_CS);
  printf("  --no-hw-cs              不执行硬件 CE0 时序测量\n");
  printf("  --bus N                 SPI 总线，默认 %u\n", DEFAULT_BUS);
  printf(
      "  --speeds A,B,...        时钟扫描，默认 1M,5M,10M,20M,25M,50M,80M\n");
  printf("  --max-transfer N        每轮有效负载字节数，默认 %u\n",
         DEFAULT_MAX_TRANSFER);
  printf("  --loops N               每档时钟的图样轮数，默认 %u\n",
         DEFAULT_PERF_LOOPS);
  printf("  --payload-loops N       48MHz 性能矩阵每项重复次数，默认 %u\n",
         DEFAULT_PAYLOAD_LOOPS);
  printf("  --freq HZ               持续输出请求频率 HZ 的 01010101 MOSI "
         "数据，Ctrl-C 停止\n");
  printf("  --cs-arm-ms N           CS 时序捕获前等待毫秒，默认 %u\n",
         DEFAULT_CS_ARM_MS);
  printf("  --ssi-source-hz N       从本机驱动/时钟框架读取到的实际 SSI "
         "源时钟（可选）\n");
  printf("  --max-probe-hz N        实际源时钟未知时的最大探测请求频率，最大 "
         "80MHz\n");
  printf(
      "  --cs-to-clk-ns N        记录仪器测得的 CS 下降沿到首个 SCLK 间隔\n");
  printf("  --quick                 将每轮有效负载限制为 65536 字节\n");
  printf("  --report FILE           指定报告路径；默认保存到 /tmp\n");
}

/**
 * 功能: 将逗号分隔的 GPIO 片选列表拆分为独立名称。
 * 参数: text - 片选列表；cs - 输出名称数组；count -
 * 输入当前数量并输出拆分后的数量。 返回值: 成功返回
 * 0，名称过长、数量超限或列表为空返回 -1。
 */
static int split_cs(const char *text, char cs[][16], unsigned int *count) {
  char copy[128];
  char *save = NULL;
  char *token;

  if (strlen(text) >= sizeof(copy))
    return -1;
  strcpy(copy, text);
  token = strtok_r(copy, ",", &save);
  while (token) {
    if (*count >= MAX_CS || strlen(token) >= sizeof(cs[0]))
      return -1;
    snprintf(cs[*count], sizeof(cs[0]), "%s", token);
    (*count)++;
    token = strtok_r(NULL, ",", &save);
  }
  return *count ? 0 : -1;
}

/**
 * 功能: 注册临时 SPI 设备、等待 spidev 节点并建立测试上下文。
 * 参数: ctx - 输出 SPI 上下文；bus - SPI 总线号；cs - GPIO
 * 片选名或硬件片选标记“-1”；spidev_bufsiz - 传输缓冲区上限。 返回值: 成功返回
 * 0，设备注册、节点等待或打开失败返回 -1。
 */
static int spi_context_init(spi_context_t *ctx, unsigned int bus,
                            const char *cs, size_t spidev_bufsiz) {
  struct spidev_register_data data;
  unsigned int retry;
  int fd;

  memset(ctx, 0, sizeof(*ctx));
  ctx->fd = -1;
  ctx->bus = bus;
  ctx->spidev_bufsiz = spidev_bufsiz ? spidev_bufsiz : 4096;
  snprintf(ctx->cs, sizeof(ctx->cs), "%.*s", (int)sizeof(ctx->cs) - 1, cs);
  memset(&data, 0, sizeof(data));
  data.busnum = (int)bus;
  data.cs_gpio = (char *)cs;
  /*
   * libhardware2 将此请求交给平台 SPI 驱动创建一个临时从设备：cs 是
   * GPIO 名称时使用软件片选，"-1" 则让驱动使用控制器的硬件 CE。成功后
   * data.spidev_path 是该设备将由 spidev 导出的节点路径。
   */
  if (spi_add_device(&data) < 0)
    return -1;

  /*
   * 注册返回时，spidev 的 probe 和 devtmpfs 创建设备节点可能仍在异步进行；
   * 因而先等待节点出现，再打开它，避免把暂时的 ENOENT 误报为驱动注册失败。
   */
  for (retry = 0; retry < DEVICE_WAIT_RETRIES; retry++) {
    if (access(data.spidev_path, F_OK) == 0)
      break;
    usleep(DEVICE_WAIT_US);
  }
  fd = spi_open(data.spidev_path);
  if (fd < 0) {
    spi_del_device(data.spidev_path);
    return -1;
  }
  ctx->fd = fd;
  snprintf(ctx->path, sizeof(ctx->path), "%s", data.spidev_path);
  return 0;
}

/**
 * 功能: 关闭 spidev 文件描述符并删除临时 SPI 设备。
 * 参数: ctx - 待释放的 SPI 上下文。
 * 返回值: 无。
 */
static void spi_context_deinit(spi_context_t *ctx) {
  /* 先关闭用户态 fd，再通知平台驱动删除临时 SPI 设备，避免释放后仍有 ioctl。 */
  if (ctx->fd >= 0)
    spi_close(ctx->fd);
  if (ctx->path[0])
    spi_del_device(ctx->path);
  ctx->fd = -1;
  ctx->path[0] = '\0';
}

/**
 * 功能: 下发 SPI 模式、频率、位宽和位序，并回读有效配置。
 * 参数: ctx - 已打开的 SPI 上下文；mode - SPI 模式位；speed - 请求频率；bpw -
 * 每字位数；lsb - 非零表示 LSB 优先。 返回值: 配置和回读一致时返回 0，否则返回
 * -1。
 */
static int configure_spi(spi_context_t *ctx, unsigned int mode,
                         unsigned int speed, unsigned int bpw,
                         unsigned int lsb) {
  struct spi_info info;

  /* libhardware2 通过 spidev ioctl 将这些配置下发到 soc_spi 驱动。 */
  if (spi_set_mode(ctx->fd, (int)mode) < 0 ||
      spi_set_speed(ctx->fd, (int)speed) < 0 ||
      spi_set_bits(ctx->fd, (int)bpw) < 0 || spi_set_lsb(ctx->fd, (int)lsb) < 0)
    return -1;
  memset(&info, 0, sizeof(info));
  spi_get_info(ctx->fd, &info);
  /* 回读驱动当前有效配置，防止不支持的模式或位宽被静默改写后继续测试。 */
  if (info.spi_bits != bpw || info.spi_lsb != lsb || info.spi_speed != speed ||
      (info.spi_mode & (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)) !=
          (mode & (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)) ||
      (info.spi_mode & SPI_LOOP) != (mode & SPI_LOOP))
    return -1;
  return 0;
}

/**
 * 功能: 通过单条 spidev 消息执行原始 SPI 传输。
 * 参数: fd - spidev 文件描述符；tx - 发送缓冲区或 NULL；rx - 接收缓冲区或
 * NULL；len - 字节数；speed - 请求频率；bpw - 每字位数；cs_change -
 * 传输后是否改变 CS。 返回值: spi_transfer() 的返回值，负值表示
 * ioctl/驱动错误。
 */
static int raw_transfer(int fd, void *tx, void *rx, size_t len,
                        unsigned int speed, unsigned int bpw,
                        unsigned int cs_change) {
  struct spi_ioc_transfer msg;

  /* 构造一个 SPI_IOC_MESSAGE(1)；spi_transfer() 将它送入当前 spidev/平台驱动。
   */
  memset(&msg, 0, sizeof(msg));
  msg.tx_buf = (unsigned long)tx;
  msg.rx_buf = (unsigned long)rx;
  msg.len = (uint32_t)len;
  msg.speed_hz = speed;
  msg.bits_per_word = (uint8_t)bpw;
  msg.cs_change = (uint8_t)cs_change;
  return spi_transfer(fd, &msg, 1);
}

/**
 * 功能: 生成下一个确定性伪随机数。
 * 参数: state - 输入并更新的随机状态。
 * 返回值: 新生成的 32 位伪随机数。
 */
static uint32_t next_random(uint32_t *state) {
  *state = *state * 1664525U + 1013904223U;
  return *state;
}

/**
 * 功能: 计算指定位宽在测试缓冲区中占用的字节槽大小。
 * 参数: bpw - 每字位数。
 * 返回值: 8 位及以下返回 1，16 位及以下返回 2，其余返回 4。
 */
static size_t bytes_per_word(unsigned int bpw) {
  return bpw <= 8 ? 1 : bpw <= 16 ? 2 : 4;
}

/**
 * 功能: 将请求传输长度向上对齐到当前位宽的字节槽边界。
 * 参数: requested - 请求字节数；bpw - 每字位数；result - 输出对齐后的字节数。
 * 返回值: 成功返回 0，结果溢出或不合法时返回 -1。
 */
static int aligned_len(size_t requested, unsigned int bpw, size_t *result) {
  size_t align = bytes_per_word(bpw);
  size_t value = requested;

  if (value < align)
    value = align;
  value = (value + align - 1) / align * align;
  *result = value;
  return value > 0 && value <= SIZE_MAX / 2 ? 0 : -1;
}

/**
 * 功能: 生成指定有效位宽的低位掩码。
 * 参数: bpw - 每字有效位数。
 * 返回值: 对应掩码；bpw 大于等于 32 时返回 UINT32_MAX。
 */
static uint32_t word_mask(unsigned int bpw) {
  return bpw >= 32 ? UINT32_MAX : ((UINT32_C(1) << bpw) - 1U);
}

/**
 * 功能: 按模式填充用于 SPI 发送或校验的测试数据。
 * 参数: buf - 输出缓冲区；len - 缓冲区字节数；bpw - 每字位数；pattern -
 * 图样索引；seed - 伪随机图样种子。 返回值: 无。
 */
static void fill_pattern(unsigned char *buf, size_t len, unsigned int bpw,
                         unsigned int pattern, uint32_t seed) {
  size_t step = bytes_per_word(bpw);
  size_t off;
  uint32_t value;
  uint32_t state = seed;
  uint32_t mask = word_mask(bpw);

  for (off = 0; off < len; off += step) {
    switch (pattern % 6) {
    case 0:
      value = 0;
      break;
    case 1:
      value = mask;
      break;
    case 2:
      value = UINT32_C(0xaaaaaaaa);
      break;
    case 3:
      value = UINT32_C(0x55555555);
      break;
    case 4:
      value = (uint32_t)(off / step);
      break;
    default:
      value = next_random(&state);
      break;
    }
    value &= mask;
    memcpy(buf + off, &value, step);
  }
}

/**
 * 功能: 按位宽比较发送和接收数据，定位第一个不一致字。
 * 参数: tx - 期望数据；rx - 接收数据；len - 比较字节数；bpw - 每字有效位数。
 * 返回值: 首个不匹配的字节偏移；全部一致返回 SIZE_MAX。
 */
static size_t first_mismatch(const unsigned char *tx, const unsigned char *rx,
                             size_t len, unsigned int bpw) {
  size_t step = bytes_per_word(bpw);
  size_t off;
  uint32_t a, b;
  uint32_t mask = word_mask(bpw);

  for (off = 0; off < len; off += step) {
    a = 0;
    b = 0;
    memcpy(&a, tx + off, step);
    memcpy(&b, rx + off, step);
    if ((a & mask) != (b & mask))
      return off;
  }
  return SIZE_MAX;
}

/**
 * 功能: 计算满足字对齐要求的单次 spidev 传输上限。
 * 参数: ctx - SPI 上下文；bpw - 每字位数。
 * 返回值: 对齐后的最大块长度，至少为一个字节槽。
 */
static size_t transfer_chunk_limit(const spi_context_t *ctx, unsigned int bpw) {
  size_t align = bytes_per_word(bpw);
  size_t limit = ctx->spidev_bufsiz;

  limit = limit / align * align;
  return limit ? limit : align;
}

/**
 * 功能: 分块执行缓冲区 SPI 传输并校验回读数据。
 * 参数: ctx - SPI 上下文；tx - 发送缓冲区或 NULL；rx - 接收缓冲区或 NULL；len -
 * 总字节数；speed - 请求频率；bpw - 每字位数；bad_offset - 输出首个错误偏移。
 * 返回值: 成功返回 0，数据不一致返回 -EBADE，传输失败返回驱动错误码。
 */
static int transfer_buffers(spi_context_t *ctx, unsigned char *tx,
                            unsigned char *rx, size_t len, unsigned int speed,
                            unsigned int bpw, size_t *bad_offset) {
  size_t offset = 0;
  size_t chunk;
  size_t mismatch;
  size_t limit = transfer_chunk_limit(ctx, bpw);
  int ret;

  while (offset < len) {
    chunk = len - offset;
    if (chunk > limit)
      chunk = limit;
    ret = raw_transfer(ctx->fd, tx ? tx + offset : NULL,
                       rx ? rx + offset : NULL, chunk, speed, bpw, 0);
    if (ret < 0) {
      if (bad_offset)
        *bad_offset = offset;
      return ret;
    }
    offset += chunk;
  }
  mismatch = SIZE_MAX;
  if (rx) {
    if (tx)
      mismatch = first_mismatch(tx, rx, len, bpw);
    else {
      size_t i;
      for (i = 0; i < len; i++) {
        if (rx[i] != 0) {
          mismatch = i;
          break;
        }
      }
    }
  }
  if (bad_offset)
    *bad_offset = mismatch;
  return mismatch == SIZE_MAX ? 0 : -EBADE;
}

/**
 * 功能: 将指定 GPIO 切换为当前 SPI 控制器的硬件 CE0 复用功能。
 * 参数: bus - SPI 总线号；gpio - 支持的 CE0 引脚名称。
 * 返回值: 成功返回 gpio_set_func() 的结果；引脚不支持返回 -EINVAL，打开 GPIO
 * 设备失败返回负 errno。
 */
static int configure_hardware_cs_pin(unsigned int bus, const char *gpio) {
  char *funcs[2];
  int fd;
  int ret;

  if (bus == 0 && !strcasecmp(gpio, "pd05"))
    funcs[0] = "func2";
  else if (bus == 0 && !strcasecmp(gpio, "pb00"))
    funcs[0] = "func0";
  else if (bus == 1 && !strcasecmp(gpio, "pc30"))
    funcs[0] = "func1";
  else if (bus == 1 && !strcasecmp(gpio, "pc18"))
    funcs[0] = "func2";
  else
    return -EINVAL;
  funcs[1] = "pull_hiz";

  /*
   * 软件 CS 阶段由 GPIO 驱动控制该引脚；硬件 CE0 阶段必须改为 SSI 复用功能，
   * 才能让 soc_spi 在帧开始和结束时驱动片选。func 编号由 X2600 引脚复用表决定。
   */
  fd = gpio_open();
  if (fd < 0)
    return -errno;
  ret = gpio_set_func(fd, gpio, funcs, 2);
  gpio_close(fd);
  return ret;
}

/**
 * 功能: 将完成硬件 CS 测试的引脚恢复为高阻输入。
 * 参数: gpio - 待恢复的 GPIO 名称。
 * 返回值: 无；无法打开 GPIO 设备时直接返回。
 */
static void restore_hardware_cs_pin(const char *gpio) {
  char *funcs[] = {"input", "pull_hiz"};
  int fd = gpio_open();

  /* 测试结束后撤销 SSI CE0 复用，避免该引脚继续由控制器输出片选电平。 */
  if (fd < 0)
    return;
  (void)gpio_set_func(fd, gpio, funcs, 2);
  gpio_close(fd);
}

typedef enum {
  PAYLOAD_LOOP,
  PAYLOAD_WRITE,
  PAYLOAD_READ,
  PAYLOAD_DUPLEX,
} payload_case_t;

/**
 * 功能: 返回有效负载性能子用例的中文名称。
 * 参数: kind - 有效负载子用例类型。
 * 返回值: 子用例名称；未知值返回“未知”。
 */
static const char *payload_case_name(payload_case_t kind) {
  switch (kind) {
  case PAYLOAD_LOOP:
    return "内部 LOOP";
  case PAYLOAD_WRITE:
    return "仅写";
  case PAYLOAD_READ:
    return "仅读";
  case PAYLOAD_DUPLEX:
    return "外部全双工";
  }
  return "未知";
}

/**
 * 功能: 在 48MHz 下执行一种读写方式和一种数据长度的性能测试。
 * 参数: ctx - SPI 上下文；opt - 测试选项；kind -
 * LOOP、仅写、仅读或全双工类型；len - 单轮字节数。 返回值: 无；结果通过
 * case_result() 记录。
 */
static void run_payload_case(spi_context_t *ctx, const test_options_t *opt,
                             payload_case_t kind, size_t len) {
  unsigned char *tx = NULL;
  unsigned char *rx = NULL;
  unsigned int spi_mode = SPI_MODE_0;
  unsigned int loop;
  unsigned long long bytes = 0;
  struct timespec start, end;
  size_t bad = SIZE_MAX;
  double ms, mbps;
  int ret = 0;
  char name[96];
  char detail[280];

  /*
   * 根据传输方向只分配驱动实际会使用的缓冲区：仅写无需 RX；仅读无需 TX；
   * 内部回环和外部全双工必须同时保存期望值与回读值，才能完成一致性校验。
   */
  if (kind == PAYLOAD_LOOP || kind == PAYLOAD_DUPLEX || kind == PAYLOAD_WRITE) {
    tx = malloc(len);
    if (!tx)
      ret = -ENOMEM;
  }
  if ((kind == PAYLOAD_LOOP || kind == PAYLOAD_DUPLEX ||
       kind == PAYLOAD_READ) &&
      !ret) {
    rx = malloc(len);
    if (!rx)
      ret = -ENOMEM;
  }
  if (ret) {
    /* 两个缓冲区可能仅成功分配其一，统一释放后把该组合标记为失败。 */
    snprintf(name, sizeof(name), "%s-%zuKiB", payload_case_name(kind),
             len / 1024);
    case_result("48MHz 性能", name, -1, "测试缓冲区分配失败");
    free(tx);
    free(rx);
    return;
  }

  /* SPI_LOOP 在控制器内部将 TX 回送到 RX，不经过 MOSI/MISO 引脚。 */
  if (kind == PAYLOAD_LOOP)
    spi_mode |= SPI_LOOP;
  /* 所有子用例固定使用 mode 0、8 bit、MSB-first 和 48 MHz 请求频率。 */
  if (configure_spi(ctx, spi_mode, 48000000U, 8, 0) < 0) {
    snprintf(name, sizeof(name), "%s-%zuKiB", payload_case_name(kind),
             len / 1024);
    case_result("48MHz 性能", name, -1, "SPI 配置失败");
    free(tx);
    free(rx);
    return;
  }

  /*
   * 计时从首轮数据准备开始，到最后一轮传输和校验结束为止。因此该指标包含
   * 用户态图样填充、spidev 分块 ioctl 与数据校验，是端到端有效吞吐而非线速。
   */
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (loop = 0; loop < opt->payload_loops && !stop_requested; loop++) {
    /* 每轮将轮次混入随机种子，避免驱动只对固定图样产生偶然正确的结果。 */
    if (tx)
      fill_pattern(tx, len, 8, 5, UINT32_C(0x6d2b79f5) ^ (uint32_t)len ^ loop);
    /* 预填充非零值，仅读时可验证驱动是否确实写入了预期的虚拟 0 数据。 */
    if (rx)
      memset(rx, 0xa5, len);
    bad = SIZE_MAX;
    /*
     * 长度超过 spidev_bufsiz 时，transfer_buffers() 会拆成多个 ioctl；它会在
     * 全双工/LOOP 模式比较 TX 与 RX，并在仅读模式检查 RX 是否全为 0。
     */
    ret = transfer_buffers(ctx, tx, rx, len, 48000000U, 8, &bad);
    /* 统计的是一次逻辑有效负载传输，不是其内部拆出的 ioctl 个数。 */
    record_transfer(len, 8, ret != 0);
    /* 第一处传输错误或数据错配即停止该组合，保留 bad 用于报告首个错误位置。 */
    if (ret)
      break;
    /* 仅完整通过的轮次计入带宽分子，失败轮次不会夸大吞吐结果。 */
    bytes += len;
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  /* 将已完成的字节数换算为 Mbit，并以实测时间计算有效带宽。 */
  ms = elapsed_ms(&start, &end);
  mbps = ms > 0 ? (bytes * 8.0 / 1000000.0) / (ms / 1000.0) : 0;
  snprintf(name, sizeof(name), "%s-%zuKiB", payload_case_name(kind),
           len / 1024);
  /* 分别报告数据校验错误、驱动/ioctl 错误和成功结果，便于区分问题层次。 */
  if (ret == -EBADE)
    snprintf(detail, sizeof(detail),
             "随机数据校验失败，错误偏移=%zu；完成=%lluB；有效带宽=%.2fMbit/s",
             bad, bytes, mbps);
  else if (ret < 0)
    snprintf(detail, sizeof(detail),
             "传输失败，返回=%d；完成=%lluB；有效带宽=%.2fMbit/s", ret, bytes,
             mbps);
  else if (kind == PAYLOAD_READ)
    snprintf(detail, sizeof(detail),
             "虚拟 0 "
             "数据仅读，完成=%lluB；有效带宽=%.2fMbit/"
             "s；外部短接只能验证接收为 0，不能替代从机随机响应",
             bytes, mbps);
  else
    snprintf(detail, sizeof(detail),
             "随机数据，轮数=%u；完成=%lluB；有效带宽=%.2fMbit/s", loop, bytes,
             mbps);
  case_result("48MHz 性能", name, ret == 0 ? 1 : -1, detail);
  /* 正常完成及所有提前返回路径都释放已分配的测试缓冲区。 */
  free(tx);
  free(rx);
}

/**
 * 功能: 执行全部 48MHz 有效负载类型和预定义长度组合。
 * 参数: ctx - SPI 上下文；opt - 测试选项。
 * 返回值: 无；收到终止信号时提前结束。
 */
static void run_payload_matrix(spi_context_t *ctx, const test_options_t *opt) {
  static const size_t lengths[] = {
      16U * 1024U,
      64U * 1024U,
      256U * 1024U,
      1024U * 1024U,
  };
  size_t i;
  payload_case_t kind;

  report_printf("\n## 48MHz 随机数据性能\n\n");
  for (kind = PAYLOAD_LOOP; kind <= PAYLOAD_DUPLEX && !stop_requested; kind++) {
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]) && !stop_requested;
         i++)
      run_payload_case(ctx, opt, kind, lengths[i]);
  }
}

/**
 * 功能: 向 soc_spi 模块的 sysfs 无符号参数写入数值。
 * 参数: name - 模块参数名称；value - 待写入数值。
 * 返回值: 成功返回 0，路径构造、打开或写入失败返回 -1。
 */
static int write_module_uint_param(const char *name, unsigned int value) {
  char path[PATH_MAX];
  char text[32];
  int fd;
  int len;

  if (snprintf(path, sizeof(path), "/sys/module/soc_spi/parameters/%s", name) >=
      (int)sizeof(path))
    return -1;
  len = snprintf(text, sizeof(text), "%u\n", value);
  if (len < 0 || len >= (int)sizeof(text))
    return -1;
  /* 写入 soc_spi 导出的模块参数；内核参数回调会在驱动上下文更新连续 DMA 状态。
   */
  fd = open(path, O_WRONLY);
  if (fd < 0)
    return -1;
  if (write(fd, text, (size_t)len) != len) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

/**
 * 功能: 配置 soc_spi 连续 DMA 并持续输出 SCLK/MOSI 测量波形。
 * 参数: bus - 要使用的 SPI 总线号；speed - 请求的 SCLK 频率，单位 Hz。
 * 返回值: 无；启动和停止结果通过 case_result() 记录。
 */
static void run_frequency_output(unsigned int bus, unsigned int speed) {
  struct timespec start, end;
  double seconds;
  char detail[240];
  int ret;

  /*
   * 先停止已有输出，再选择控制器和频率，最后才使能连续 DMA。这样不会让驱动
   * 在旧配置与新配置之间输出波形。Ctrl-C 后必须再次写 0 释放驱动持有的 DMA。
   */
  ret = write_module_uint_param("continuous_enable", 0);
  if (!ret)
    ret = write_module_uint_param("continuous_spi_id", bus);
  if (!ret)
    ret = write_module_uint_param("continuous_clk_rate", speed);
  if (!ret)
    ret = write_module_uint_param("continuous_enable", 1);
  if (ret) {
    case_result(
        "频率输出", "连续 DMA 启动", -1,
        "无法写 continuous_* 参数；请使用包含连续 DMA 支持的新 soc_spi.ko");
    return;
  }

  printf("连续 DMA 输出：请求 SCLK=%u Hz，MOSI=01010101，按 Ctrl-C 停止。\n",
         speed);
  printf("内核循环 DMA 持续供数，不使用 spidev 分块传输。\n");
  clock_gettime(CLOCK_MONOTONIC, &start);
  while (!stop_requested)
    usleep(100000);
  ret = write_module_uint_param("continuous_enable", 0);
  clock_gettime(CLOCK_MONOTONIC, &end);
  seconds = elapsed_ms(&start, &end) / 1000.0;
  snprintf(detail, sizeof(detail),
           "请求频率=%u Hz；MOSI=01010101；持续 DMA 输出=%.3fs%s", speed,
           seconds, ret ? "；停止连续 DMA 失败" : "");
  case_result("频率输出", "连续 DMA 输出", ret ? -1 : 1, detail);
}

/**
 * 功能: 累计一次传输的次数、字节数和数据错误统计。
 * 参数: len - 本次字节数；bpw - 每字位数，当前仅保留接口；data_errors -
 * 本次数据错误数量。 返回值: 无。
 */
static void record_transfer(size_t len, unsigned int bpw, int data_errors) {
  report.transfers++;
  report.bytes += len;
  report.errors += (unsigned long long)data_errors;
  (void)bpw;
}

/**
 * 功能: 读取单行文本文件并去除末尾换行符。
 * 参数: path - 文件路径；value - 输出缓冲区；size - 输出缓冲区大小。
 * 返回值: 成功返回 0，打开或读取失败返回 -1。
 */
static int read_text_value(const char *path, char *value, size_t size) {
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
  while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r'))
    value[--len] = '\0';
  return 0;
}

/**
 * 功能: 读取并解析 soc_spi 模块的无符号参数。
 * 参数: name - 模块参数名称。
 * 返回值: 参数值；路径、读取或解析失败时返回 0。
 */
static unsigned int read_module_uint_param(const char *name) {
  char path[PATH_MAX];
  char value[32];
  unsigned int result;

  if (snprintf(path, sizeof(path), "/sys/module/soc_spi/parameters/%s", name) >=
      (int)sizeof(path))
    return 0;
  if (read_text_value(path, value, sizeof(value)) < 0 ||
      parse_uint(value, &result) < 0)
    return 0;
  return result;
}

/**
 * 功能: 返回数据图样索引对应的名称。
 * 参数: pattern - 图样索引，可超出图样数量。
 * 返回值: 循环映射后的图样名称。
 */
static const char *pattern_name(unsigned int pattern) {
  static const char *const names[] = {"全 00", "全 FF", "AA",
                                      "55",    "递增",  "伪随机"};

  return names[pattern % (sizeof(names) / sizeof(names[0]))];
}

/**
 * 功能: 读取并打印当前 soc_spi 模块的关键诊断参数。
 * 参数: bus - 要查询引脚状态的 SPI 总线号。
 * 返回值: 无；不可读取的参数以文本标识。
 */
static void print_loaded_driver_parameters(unsigned int bus) {
  const char *names[6];
  char enabled[24], clk[24], miso[24], mosi[24];
  char path[PATH_MAX];
  char value[32];
  char detail[240];
  size_t i;
  size_t used = 0;

  snprintf(enabled, sizeof(enabled), "spi%u_is_enable", bus);
  snprintf(clk, sizeof(clk), "spi%u_clk", bus);
  snprintf(miso, sizeof(miso), "spi%u_miso", bus);
  snprintf(mosi, sizeof(mosi), "spi%u_mosi", bus);
  names[0] = "div_ssi_rate";
  names[1] = "actual_ssi_rate";
  names[2] = enabled;
  names[3] = clk;
  names[4] = miso;
  names[5] = mosi;

  detail[0] = '\0';
  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    int n;

    if (snprintf(path, sizeof(path), "/sys/module/soc_spi/parameters/%s",
                 names[i]) >= (int)sizeof(path) ||
        read_text_value(path, value, sizeof(value)) < 0)
      snprintf(value, sizeof(value), "(不可读取)");
    n = snprintf(detail + used, sizeof(detail) - used, "%s%s=%s",
                 used ? " " : "", names[i], value);
    if (n < 0 || (size_t)n >= sizeof(detail) - used)
      break;
    used += (size_t)n;
  }
  printf("[诊断] 已加载 soc_spi: %s\n", detail);
  report_printf("- 已加载 soc_spi: %s\n", detail);
}

/**
 * 功能: 输出回环传输失败的错误位置及相邻数据。
 * 参数: speed - 请求频率；loop - 当前轮次；loops - 总轮数；bad -
 * 首个错误偏移；tx - 期望数据；rx - 接收数据；len - 缓冲区长度；transfer_ret -
 * 传输返回值；transfer_errno - 失败时的 errno。 返回值: 无。
 */
static void print_loopback_failure(unsigned int speed, unsigned int loop,
                                   unsigned int loops, size_t bad,
                                   const unsigned char *tx,
                                   const unsigned char *rx, size_t len,
                                   int transfer_ret, int transfer_errno) {
  size_t start = 0;
  size_t end = 0;
  size_t i;

  if (bad != SIZE_MAX && bad < len) {
    start = bad > 4 ? bad - 4 : 0;
    end = start + 16;
    if (end > len)
      end = len;
    printf("[诊断] %uHz 第 %u/%u 轮 图样=%s 返回=%d 首个错误=%zu "
           "期望=0x%02x 接收=0x%02x\n",
           speed, loop + 1, loops, pattern_name(loop), transfer_ret, bad,
           tx[bad], rx[bad]);
    printf("[诊断] 字节[%zu..%zu): 发送", start, end);
    for (i = start; i < end; i++)
      printf(" %02x", tx[i]);
    printf(" 接收");
    for (i = start; i < end; i++)
      printf(" %02x", rx[i]);
    printf("\n");
  } else {
    printf(
        "[诊断] %uHz 第 %u/%u 轮 图样=%s ioctl 失败: 返回=%d errno=%d (%s)\n",
        speed, loop + 1, loops, pattern_name(loop), transfer_ret,
        transfer_errno, strerror(transfer_errno));
  }
}

/**
 * 功能: 将需要仪器确认的测量项写入终端和报告。
 * 参数: category - 测量分类；name - 测量项名称；detail - 测量条件或结果说明。
 * 返回值: 无。
 */
static void instrument_result(const char *category, const char *name,
                              const char *detail) {
  printf("[测量] %s/%s%s%s\n", category, name, detail && detail[0] ? " - " : "",
         detail && detail[0] ? detail : "");
  report_printf("| 待仪器测量 | %s | %s | %s |\n", category, name,
                detail && detail[0] ? detail : "");
}

/**
 * 功能: 扫描时钟频率并统计最大无错频率及有效负载带宽。
 * 参数: ctx - SPI 上下文；opt - 测试选项；ssi_source_hz - 实际 SSI
 * 源时钟；requested_ssi_hz - 驱动请求源时钟；spi_mode - 本轮使用的 SPI 模式。
 * 返回值: 包含最高无错频率和最后有效带宽的性能结果。
 */
static performance_result_t run_clock_performance(spi_context_t *ctx,
                                                  const test_options_t *opt,
                                                  unsigned int ssi_source_hz,
                                                  unsigned int requested_ssi_hz,
                                                  unsigned int spi_mode) {
  performance_result_t result;
  unsigned int speeds[MAX_SPEEDS + 1];
  size_t speed_count = opt->speed_count;
  size_t i;
  size_t len = opt->max_transfer;
  unsigned int sclk_limit = max_supported_sclk(ssi_source_hz);
  struct timespec start, end;
  double ms, mbps;
  char name[64];
  char detail[260];

  memset(&result, 0, sizeof(result));

  if (aligned_len(len, 8, &len) < 0)
    return result;
  for (i = 0; i < opt->speed_count; i++)
    speeds[i] = opt->speeds[i];
  if (ssi_source_hz) {
    int have_max_speed = 0;

    for (i = 0; i < opt->speed_count; i++) {
      if (speeds[i] == sclk_limit)
        have_max_speed = 1;
    }
    if (!have_max_speed && speed_count < sizeof(speeds) / sizeof(speeds[0]))
      speeds[speed_count++] = sclk_limit;
    snprintf(detail, sizeof(detail),
             "请求的 div_ssi_rate=%u Hz；实际 SSI 源时钟=%u Hz；"
             "不超过 SSI 80MHz 规格的最高分频 SCLK=%u Hz",
             requested_ssi_hz, ssi_source_hz, sclk_limit);
    case_result("时钟", "理论上限", 1, detail);
  } else {
    case_result("时钟", "理论上限", 0,
                "实际 SSI 源时钟未知；各请求频率必须由当前 SPI CLK "
                "引脚上的仪器实测确认");
  }

  for (i = 0; i < speed_count && !stop_requested; i++) {
    unsigned int speed = speeds[i];
    unsigned int loop;
    unsigned long long bytes = 0;
    unsigned char *tx = malloc(len);
    unsigned char *rx = malloc(len);
    int failed = 0;
    int transfer_ret = 0;
    int transfer_errno = 0;
    size_t bad = SIZE_MAX;
    unsigned int failed_loop = 0;
    unsigned char failed_expected = 0;
    unsigned char failed_received = 0;
    int has_mismatch = 0;

    if (sclk_limit && speed > sclk_limit) {
      snprintf(name, sizeof(name), "%uHz", speed);
      snprintf(detail, sizeof(detail), "请求频率超过理论 SCLK 上限 %u Hz",
               sclk_limit);
      case_result("时钟与带宽", name, 0, detail);
      free(tx);
      free(rx);
      continue;
    }
    if (!tx || !rx) {
      free(tx);
      free(rx);
      snprintf(name, sizeof(name), "%uHz-memory", speed);
      case_result("频率", name, -1, "测试 buffer 分配失败");
      continue;
    }
    if (configure_spi(ctx, spi_mode, speed, 8, 0) < 0) {
      snprintf(name, sizeof(name), "%uHz-config", speed);
      case_result("频率", name, -1, "配置失败");
      free(tx);
      free(rx);
      continue;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (loop = 0; loop < opt->performance_loops; loop++) {
      fill_pattern(tx, len, 8, loop, UINT32_C(0x13579bdf) + loop);
      memset(rx, 0, len);
      bad = SIZE_MAX;
      errno = 0;
      transfer_ret = transfer_buffers(ctx, tx, rx, len, speed, 8, &bad);
      transfer_errno = errno;
      if (transfer_ret != 0) {
        failed = 1;
        failed_loop = loop;
        if (bad != SIZE_MAX && bad < len) {
          failed_expected = tx[bad];
          failed_received = rx[bad];
          has_mismatch = 1;
        }
        print_loopback_failure(speed, loop, opt->performance_loops, bad, tx, rx,
                               len, transfer_ret, transfer_errno);
        record_transfer(len, 8, 1);
        break;
      }
      record_transfer(len, 8, 0);
      bytes += len;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    free(tx);
    free(rx);
    ms = elapsed_ms(&start, &end);
    mbps = ms > 0 ? (bytes * 8.0 / 1000000.0) / (ms / 1000.0) : 0;
    snprintf(name, sizeof(name), "%uHz", speed);
    if (failed && has_mismatch) {
      snprintf(detail, sizeof(detail),
               "有效负载=%zuB 轮数=%u 已检查=%lluB 有效带宽=%.2fMbit/s "
               "失败轮次=%u 图样=%s 首个错误=%zu 期望=0x%02x 接收=0x%02x",
               len, opt->performance_loops, bytes, mbps, failed_loop + 1,
               pattern_name(failed_loop), bad, failed_expected,
               failed_received);
    } else if (failed) {
      snprintf(detail, sizeof(detail),
               "有效负载=%zuB 轮数=%u 已检查=%lluB 有效带宽=%.2fMbit/s "
               "失败轮次=%u 图样=%s ioctl 返回=%d errno=%d",
               len, opt->performance_loops, bytes, mbps, failed_loop + 1,
               pattern_name(failed_loop), transfer_ret, transfer_errno);
    } else {
      snprintf(detail, sizeof(detail),
               "有效负载=%zuB 轮数=%u 已检查=%lluB 有效带宽=%.2fMbit/s", len,
               opt->performance_loops, bytes, mbps);
    }
    case_result("时钟与带宽", name, failed ? -1 : 1, detail);
    printf("    请求 %u Hz: %.2f Mbit/s (%s)\n", speed, mbps,
           failed ? "失败" : "通过");
    if (!failed && speed >= result.highest_passing_speed) {
      result.highest_passing_speed = speed;
      result.payload_mbps = mbps;
    }
  }

  if (result.highest_passing_speed) {
    snprintf(detail, sizeof(detail),
             "最高无错请求频率=%u Hz；有效负载带宽=%.2f Mbit/s；模式=%s",
             result.highest_passing_speed, result.payload_mbps,
             spi_mode & SPI_LOOP ? "SPI_LOOP 内部回环" : "外部回环");
    case_result("时钟", "最高无错档", 1, detail);
  } else {
    case_result("时钟", "最高无错档", -1,
                "没有任何时钟档位通过四图样回环校验，不能得出带宽结论");
  }

  return result;
}

/**
 * 功能: 提示操作者输入一项无符号仪器测量值。
 * 参数: prompt - 提示文本；value - 成功时写入测量值。
 * 返回值: 输入有效数值返回 1，直接回车或输入结束返回 0，格式错误返回 -1。
 */
static int prompt_uint_measurement(const char *prompt, unsigned int *value) {
  char line[64];
  size_t len;

  printf("%s (直接回车表示未记录): ", prompt);
  fflush(stdout);
  if (!fgets(line, sizeof(line), stdin))
    return 0;
  len = strcspn(line, "\r\n");
  line[len] = '\0';
  if (!line[0])
    return 0;
  return parse_uint(line, value) == 0 ? 1 : -1;
}

/**
 * 功能: 产生一次可由示波器或逻辑分析仪触发的 CS/SCLK 采样传输。
 * 参数: ctx - SPI 上下文；opt - 测试选项；capture_speed - 请求频率；cs_label -
 * 被测 CS 名称；use_saved_timing - 非零时优先使用命令行提供的时序值。 返回值:
 * 无；传输和仪器记录结果写入报告。
 */
static void run_instrument_capture(spi_context_t *ctx,
                                   const test_options_t *opt,
                                   unsigned int capture_speed,
                                   const char *cs_label, int use_saved_timing) {
  unsigned char *tx;
  size_t len = ctx->spidev_bufsiz < 4096 ? ctx->spidev_bufsiz : 4096;
  char clk[32] = "当前 SPI CLK";
  char path[PATH_MAX];
  char detail[280];
  unsigned int measured;
  int ret;

  if (!capture_speed || !len) {
    case_result("CS 时序", "采样传输", 0,
                "没有可用时钟，未产生 CS/SCLK 测量触发");
    return;
  }
  tx = malloc(len);
  if (!tx) {
    free(tx);
    case_result("CS 时序", "采样传输", -1, "测试 buffer 分配失败");
    return;
  }
  if (snprintf(path, sizeof(path), "/sys/module/soc_spi/parameters/spi%u_clk",
               ctx->bus) < (int)sizeof(path))
    read_text_value(path, clk, sizeof(clk));
  ret = configure_spi(ctx, SPI_MODE_0, capture_speed, 8, 0);
  if (ret == 0) {
    fill_pattern(tx, len, 8, 3, UINT32_C(0x5a5a5a5a));
    /*
     * 当前 soc_spi 驱动可能在 cs_change=0 的消息后保持 CS 有效。先发送一个
     * 未触发的短消息并以 cs_change=1 结束，使后续仪器采样从独立的 CS 高电平
     * 开始，能够稳定捕获一次新的 CS 下降沿。
     */
    ret = raw_transfer(ctx->fd, tx, NULL, 1, capture_speed, 8, 1);
  }
  if (ret == 0) {
    printf("[仪器] 探头接 CS=%s 与 CLK=%s，触发条件设为 CS 下降沿。\n",
           cs_label, clk);
    printf("[仪器] 按 Enter 后将在 %u ms 后发起一次 %zuB 的普通 SPI 发送。",
           opt->cs_arm_ms, len);
    fflush(stdout);
    (void)getchar();
    printf("\nCS/SCLK 采样将在 %u ms 后开始\n", opt->cs_arm_ms);
    usleep(opt->cs_arm_ms * 1000U);
    ret = raw_transfer(ctx->fd, tx, NULL, len, capture_speed, 8, 1);
  }
  snprintf(detail, sizeof(detail),
           "CS=%s CLK=%s 普通模式仅发送 请求频率=%uHz 单次传输=%zuB", cs_label,
           clk, capture_speed, len);
  case_result("仪器", "采样传输", ret >= 0 ? 1 : -1, detail);
  if (ret >= 0) {
    if (prompt_uint_measurement("输入仪器测得的 SCLK 频率(Hz)", &measured) >
        0) {
      snprintf(detail, sizeof(detail),
               "实测=%u Hz；请求频率=%u Hz；探测 CLK=%s", measured,
               capture_speed, clk);
      instrument_result("SCLK", "频率", detail);
      snprintf(detail, sizeof(detail), "实测=%u Hz；SSI I/O 规格上限=%u Hz",
               measured, SSI_IO_MAX_HZ);
      case_result("SCLK", "I/O 规格上限", measured <= SSI_IO_MAX_HZ ? 1 : -1,
                  detail);
    } else {
      snprintf(detail, sizeof(detail),
               "请求频率=%u Hz；探测 CLK=%s；未记录实测值", capture_speed, clk);
      instrument_result("SCLK", "频率", detail);
    }
    if (use_saved_timing && opt->cs_to_clk_ns_valid) {
      snprintf(detail, sizeof(detail),
               "实测=%u ns；从 CS 下降沿量至首个 CLK 边沿；CS=%s CLK=%s",
               opt->cs_to_clk_ns, cs_label, clk);
      instrument_result("CS 时序", "CS 下降沿到首个 SCLK", detail);
    } else {
      int prompt_ret = prompt_uint_measurement(
          "输入仪器测得的 CS 下降沿到首个 SCLK 边沿间隔(ns)", &measured);

      if (prompt_ret > 0) {
        snprintf(detail, sizeof(detail), "实测=%u ns；CS=%s CLK=%s", measured,
                 cs_label, clk);
      } else {
        snprintf(detail, sizeof(detail), "CS=%s CLK=%s；未记录实测值", cs_label,
                 clk);
      }
      instrument_result("CS 时序", "CS 下降沿到首个 SCLK", detail);
    }
  }
  free(tx);
}

/**
 * 功能: 在 1MHz、10MHz 和 48MHz 下依次触发 CS 到首个 SCLK 的仪器测量。
 * 参数: ctx - SPI 上下文；opt - 测试选项；cs_label - 被测 CS 名称；cs_kind -
 * 软件或硬件 CS 类型说明。 返回值: 无；收到终止信号时提前结束。
 */
static void run_cs_timing_sweep(spi_context_t *ctx, const test_options_t *opt,
                                const char *cs_label, const char *cs_kind) {
  static const unsigned int speeds[MAX_TIMING_SPEEDS] = {
      1000000U,
      10000000U,
      48000000U,
  };
  size_t i;

  report_printf("\n## %s CS 到 SCLK 时序\n\n", cs_kind);
  for (i = 0; i < MAX_TIMING_SPEEDS && !stop_requested; i++) {
    printf("  [%s] 请求 %u Hz：测量 CS 下降沿到首个 SCLK\n", cs_kind,
           speeds[i]);
    run_instrument_capture(ctx, opt, speeds[i], cs_label, 0);
  }
}

/**
 * 功能: 解析参数、建立 SPI 测试资源、执行所选模式并按资源顺序清理。
 * 参数: argc - 命令行参数数量；argv - 命令行参数数组。
 * 返回值: 参数或报告初始化失败返回 2；存在失败用例返回 1；其余情况返回 0。
 */
int main(int argc, char **argv) {
  test_options_t opt;
  spi_context_t contexts[MAX_CS];
  char cs_names[MAX_CS][16];
  unsigned int cs_count = 0;
  unsigned int i;
  size_t spidev_bufsiz;
  unsigned int requested_ssi_hz;
  unsigned int ssi_source_hz;
  unsigned int capture_speed;
  performance_result_t performance;
  spi_context_t hw_context;
  int hw_context_ready = 0;
  int hw_cs_configured = 0;
  int delay_software;
  int delay_hardware;
  int need_software_context;
  int parse_ret;
  const char *driver_path =
      "/home/devvean/work/linux/module_driver/soc/x2600_510/spi/spi.c";

  parse_ret = parse_args(argc, argv, &opt);
  if (parse_ret != 0) {
    print_usage(argv[0]);
    return parse_ret > 0 ? 0 : 2;
  }
  delay_software = opt.mode == TEST_MODE_DELAY &&
                   (!opt.hw_cs_specified || opt.soft_cs_specified);
  delay_hardware = opt.mode == TEST_MODE_DELAY && opt.hw_cs_specified &&
                   opt.hardware_cs_enabled;
  need_software_context =
      (opt.mode != TEST_MODE_DELAY && opt.mode != TEST_MODE_FREQUENCY) ||
      delay_software;
  if (split_cs(opt.cs_text, cs_names, &cs_count) < 0) {
    fprintf(stderr, "无效 --cs 列表\n");
    return 2;
  }
  spidev_bufsiz = detect_spidev_bufsiz();
  if (report_open(&opt, driver_path) < 0)
    return 2;
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  report_printf(
      "## 测试参数\n\n- 模式=%s\n- SPI 总线=%u\n- 软件 CS=%s\n- 硬件 CE0=%s\n- "
      "硬件 CE0 测试=%s\n- 连续输出请求频率=%u Hz\n- spidev 缓冲区=%zu\n- "
      "每轮有效负载=%u\n- 时钟扫描图样轮数=%u\n- 48MHz 性能矩阵轮数=%u\n- CS "
      "采样准备时间=%u ms\n- SSI 源时钟手动值=%u Hz\n- "
      "源时钟未知时的最大探测请求=%u Hz\n\n",
      test_mode_name(opt.mode), opt.bus, opt.cs_text, opt.hw_cs_text,
      opt.hardware_cs_enabled ? "开启" : "关闭", opt.frequency_output_hz,
      spidev_bufsiz, opt.max_transfer, opt.performance_loops, opt.payload_loops,
      opt.cs_arm_ms, opt.ssi_source_hz, opt.max_probe_hz);
  report_printf(
      "## 判定口径\n\n- 最大时钟: 仅当本机可读取 "
      "`actual_ssi_rate`，或由用户通过 `--ssi-source-hz` 明确提供时，程序才按 "
      "`源时钟 / (2 * ceil(源时钟 / (2 * 80MHz)))` 计算不超过 SSI 80MHz "
      "规格的最高分频档。\n- 48MHz 性能: 内部 "
      "LOOP、仅写、仅读、外部全双工均以随机数据（仅读发送虚拟 0）测量 "
      "16KiB、64KiB、256KiB、1MiB 的用户态有效带宽。\n- CS 时序: 软件 CS 为 "
      "GPIO，硬件 CS 为当前 SPI 总线的 SSI_CE0；均在 1MHz、10MHz、48MHz "
      "下触发单次传输。纳秒值必须由仪器测量。\n\n");
  report_printf(
      "## 结果明细\n\n| 状态 | 类别 | 用例 | 详情 |\n|---|---|---|---|\n");

  memset(contexts, 0, sizeof(contexts));
  if (need_software_context) {
    for (i = 0; i < cs_count; i++) {
      contexts[i].fd = -1;
      if (spi_context_init(&contexts[i], opt.bus, cs_names[i], spidev_bufsiz) <
          0) {
        char detail[100];
        snprintf(detail, sizeof(detail), "CS=%.*s 注册/打开失败",
                 (int)sizeof(detail) - 30, cs_names[i]);
        case_result("初始化", cs_names[i], i == 0 ? -1 : 0, detail);
        if (i == 0)
          goto out;
        cs_count = i;
        break;
      }
      case_result("初始化", cs_names[i], 1, contexts[i].path);
    }
  } else {
    cs_count = 0;
  }
  if (need_software_context && (!cs_count || contexts[0].fd < 0))
    goto out;

  printf("开始 X2600 SPI 性能测试...\n");
  print_loaded_driver_parameters(opt.bus);
  requested_ssi_hz = read_module_uint_param("div_ssi_rate");
  ssi_source_hz = opt.ssi_source_hz ? opt.ssi_source_hz
                                    : read_module_uint_param("actual_ssi_rate");
  if (requested_ssi_hz && ssi_source_hz && requested_ssi_hz != ssi_source_hz)
    printf("[诊断] 请求 div_ssi_rate=%u Hz，实际 SSI 源时钟=%u Hz\n",
           requested_ssi_hz, ssi_source_hz);
  if (!ssi_source_hz)
    printf("[诊断] 无法读取实际 SSI 源时钟；请在确认驱动/时钟框架数据后使用 "
           "--ssi-source-hz 传入。\n");

  if (opt.mode == TEST_MODE_FREQUENCY) {
    run_frequency_output(opt.bus, opt.frequency_output_hz);
  } else if (opt.mode == TEST_MODE_DELAY) {
    if (delay_software) {
      printf("开始软件 GPIO CS 时序测试：CS=%s\n", opt.cs_text);
      run_cs_timing_sweep(&contexts[0], &opt, opt.cs_text, "软件 GPIO CS");
    }
    if (delay_hardware) {
      char detail[220];

      /* --cs -1 不申请 GPIO；它使 spi_add_device() 选择当前 SSI 的硬件 CE0。 */
      if (configure_hardware_cs_pin(opt.bus, opt.hw_cs_text) < 0) {
        snprintf(detail, sizeof(detail),
                 "无法将 %s 设置为 SPI%u_CE0；SPI0 支持 PD05/PB00，SPI1 支持 "
                 "PC30/PC18",
                 opt.hw_cs_text, opt.bus);
        case_result("硬件 CE0", "引脚复用", -1, detail);
      } else {
        hw_cs_configured = 1;
        if (spi_context_init(&hw_context, opt.bus, "-1", spidev_bufsiz) < 0) {
          snprintf(detail, sizeof(detail),
                   "硬件 CE0=%s 已复用，但使用 --cs -1 注册设备失败",
                   opt.hw_cs_text);
          case_result("硬件 CE0", "初始化", -1, detail);
          restore_hardware_cs_pin(opt.hw_cs_text);
          hw_cs_configured = 0;
        } else {
          hw_context_ready = 1;
          printf("开始硬件 SSI%u_CE0 时序测试：CE0=%s\n", opt.bus,
                 opt.hw_cs_text);
          run_cs_timing_sweep(&hw_context, &opt, opt.hw_cs_text,
                              "硬件 SSI_CE0");
        }
      }
    }
    if (!delay_software && !delay_hardware)
      case_result("CS 时序", "模式参数", -1,
                  "delay 模式需要 --cs GPIO 或 --hw-cs GPIO");
  } else if (opt.mode == TEST_MODE_INSTRUMENT) {
    char detail[220];

    capture_speed =
        ssi_source_hz ? max_supported_sclk(ssi_source_hz) : opt.max_probe_hz;
    if (ssi_source_hz) {
      snprintf(
          detail, sizeof(detail),
          "实际 SSI 源时钟=%u Hz；不超过 SSI 80MHz 规格的最高分频 SCLK=%u Hz",
          ssi_source_hz, capture_speed);
      case_result("时钟", "理论上限", 1, detail);
    } else {
      snprintf(detail, sizeof(detail),
               "实际 SSI 源时钟未知；请求当前 SPI CLK 探测频率=%u "
               "Hz；请实测该引脚以确认实际 SCLK",
               capture_speed);
      case_result("时钟", "理论上限", 0, detail);
    }
    printf("  [仪器] 普通 SPI 波形与交互式仪器记录\n");
    run_instrument_capture(&contexts[0], &opt, capture_speed, opt.cs_text, 1);
  } else {
    unsigned int spi_mode = SPI_MODE_0;

    if (opt.mode == TEST_MODE_INTERNAL)
      spi_mode |= SPI_LOOP;
    printf("  [%s] %s回环：最大无错请求频率与有效负载带宽\n",
           opt.mode == TEST_MODE_COMPLETE ? "1/2" : "1/1",
           opt.mode == TEST_MODE_INTERNAL ? "内部" : "外部");
    performance = run_clock_performance(&contexts[0], &opt, ssi_source_hz,
                                        requested_ssi_hz, spi_mode);
    capture_speed = performance.highest_passing_speed;
    if (!capture_speed)
      capture_speed =
          ssi_source_hz ? max_supported_sclk(ssi_source_hz) : opt.max_probe_hz;
    if (opt.mode == TEST_MODE_INTERNAL) {
      case_result("仪器", "外部波形", 0,
                  "SPI_LOOP 内部回环不在引脚输出有效 CS/SCLK/MOSI；请使用 "
                  "--mode instrument");
    } else if (opt.mode == TEST_MODE_COMPLETE) {
      if (!performance.highest_passing_speed) {
        case_result("仪器", "测量前提", -1,
                    "外部回环未找到无错频率，停止 48MHz 性能和 CS 时序测试");
        goto out;
      }
      if (capture_speed != 48000000U) {
        char detail[200];

        snprintf(
            detail, sizeof(detail),
            "当前最高无错档为 %u Hz，不是预期的 48MHz；跳过固定 48MHz 性能矩阵",
            capture_speed);
        case_result("48MHz 性能", "测试前提", 0, detail);
      } else {
        printf("  [2/4] 48MHz：随机数据 LOOP、读写与全双工性能\n");
        run_payload_matrix(&contexts[0], &opt);
      }

      printf("  [3/4] 软件 GPIO CS：1MHz、10MHz、48MHz 时序\n");
      run_cs_timing_sweep(&contexts[0], &opt, opt.cs_text, "软件 GPIO CS");

      if (opt.hardware_cs_enabled) {
        char detail[220];

        /*
         * 同一引脚不能同时作为 GPIO 软件 CS 和 SSI CE0。若两者相同，先关闭
         * 软件 CS 对应的 spidev 设备，释放 GPIO 所有权，再切换到硬件复用。
         */
        if (!strcasecmp(opt.cs_text, opt.hw_cs_text) && contexts[0].fd >= 0)
          spi_context_deinit(&contexts[0]);
        if (configure_hardware_cs_pin(opt.bus, opt.hw_cs_text) < 0) {
          snprintf(detail, sizeof(detail),
                   "无法将 %s 设置为 SPI%u_CE0；SPI0 支持 PD05/PB00，SPI1 支持 "
                   "PC30/PC18",
                   opt.hw_cs_text, opt.bus);
          case_result("硬件 CE0", "引脚复用", -1, detail);
        } else {
          hw_cs_configured = 1;
          if (spi_context_init(&hw_context, opt.bus, "-1", spidev_bufsiz) < 0) {
            snprintf(detail, sizeof(detail),
                     "硬件 CE0=%s 已复用，但使用 --cs -1 注册设备失败",
                     opt.hw_cs_text);
            case_result("硬件 CE0", "初始化", -1, detail);
            restore_hardware_cs_pin(opt.hw_cs_text);
            hw_cs_configured = 0;
          } else {
            hw_context_ready = 1;
            snprintf(detail, sizeof(detail),
                     "CE0=%s；驱动不再拉 GPIO CS，SSI 硬件帧信号负责片选",
                     opt.hw_cs_text);
            case_result("硬件 CE0", "初始化", 1, detail);
            printf("  [4/4] 硬件 SSI%u_CE0：1MHz、10MHz、48MHz 时序\n",
                   opt.bus);
            run_cs_timing_sweep(&hw_context, &opt, opt.hw_cs_text,
                                "硬件 SSI_CE0");
          }
        }
      } else {
        case_result("硬件 CE0", "时序", 0, "用户通过 --no-hw-cs 关闭");
      }
    } else {
      char detail[220];

      snprintf(detail, sizeof(detail),
               "外部数据回环完成；请用 --mode instrument 在 %u Hz 下进行 "
               "CS/CLK 示波器或逻辑分析仪测量",
               capture_speed);
      instrument_result("仪器", "下一步", detail);
    }
  }

out:
  /* 按取得资源的相反顺序释放：删除硬件 CE 设备、恢复 pinmux，再删除软件 CS
   * 设备。 */
  if (hw_context_ready)
    spi_context_deinit(&hw_context);
  if (hw_cs_configured)
    restore_hardware_cs_pin(opt.hw_cs_text);
  for (i = 0; i < cs_count; i++)
    spi_context_deinit(&contexts[i]);
  report_close();
  printf("\nSPI 测试完成: 通过=%lu 失败=%lu 跳过=%lu 传输=%lu 错误=%llu\n报告: "
         "%s\n",
         report.pass, report.fail, report.skip, report.transfers, report.errors,
         report.path[0] ? report.path : "(none)");
  return report.fail ? 1 : 0;
}
