# X2600 SPI 60MHz 测试

`app_spi` 提供 CS 时序测量、快速完整测试、CGV=0 单档超频测试和 60MHz 正式资格测试。

## 硬件条件

`x2600_nor_5.10_defconfig` 同时启用两路控制器，并将 `div_ssi` 请求为
120MHz。SSI 最快分频档对应 60MHz SCLK。将板端结果作为引脚级结论前，必须用
示波器确认实际 SCLK 为 60MHz。

| 总线 | SCLK | MOSI | MISO | 软件 CS | 硬件 CE0 |
|---|---|---|---|---|---|
| SPI0 | PD00 | PD01 | PD02 | PC09 | PD05 |
| SPI1 | PC25 | PC26 | PC27 | PC30 | PC30 |

每次外部全双工测试均须在对应总线的 SoC 侧短接 MOSI 与 MISO，并使仪器和开发板
共地。两路 SPI 共用 SSI 源时钟，因此资格测试会先后执行 SPI0、SPI1，而不会并发。

SPI1 的 PC25-PC30 与 MSC1 复用。测试 SPI1 前保持 `MD_X2600_510_MSC` 关闭，
并确认未加载 `soc_msc`。

## 构建

```sh
make test
make app
```

`make test` 在主机执行样本统计单元测试；`make app` 使用配置好的 Buildroot
交叉编译器生成 MIPS 程序。

## 快速完整测试

快速完整测试在外部全双工数据校验下扫描
1/5/10/20/25/50/60MHz。每档使用四种数据图样，报告最高无错请求频率和有效负载
带宽。

```sh
# SPI0
./app_spi --mode complete --bus 0 --cs pc09 --ssi-source-hz 120000000 --report /tmp/spi0_complete.md

# SPI1
./app_spi --mode complete --bus 1 --cs pc30 --ssi-source-hz 120000000 --report /tmp/spi1_complete.md
```

报告带宽是用户态有效负载带宽，包含图样生成、spidev 分块、ioctl 和 RX 数据比较，
不是 SCLK 的原始线速。

## CGV=0 单档超频测试

每次重新编译固件并使用新的 `MD_X2600_510_SPI_CLK_RATE` 加载 `soc_spi.ko` 后，使用
`max-sclk` 模式验证该 `div_ssi` 配置下的最高 `CGV=0` SPI 速率。程序按 MPLL=1800MHz
和整数 `SSICDR` 分频计算理论实际频率：

```text
SSICDR 分频 = round(1800000000 / MD_X2600_510_SPI_CLK_RATE)
实际 div_ssi = 1800000000 / SSICDR 分频
理论实际 SCLK = 实际 div_ssi / 2       (CGV=0)
```

例如请求 `160000000`Hz 时，整数分频为 11，理论实际 `div_ssi=163636363`Hz，理论实际
SCLK=`81818181`Hz。所有计算和 SPI 请求均使用整数 Hz；小数 MHz 仅用于人工换算。

```sh
./app_spi --mode max-sclk --div-ssi-rate 160000000 --bus 1 --cs pc30 --report /tmp/spi-max-sclk.md
```

该模式只执行一个 CGV=0 频率档，使用与快速完整测试相同的四种数据图样、外部 MOSI/MISO
全双工逐字节比较和 `--loops` 轮测试；`--max-transfer` 和 `--loops` 同样适用于该模式。
报告会回显输入的 `div_ssi_rate`、整数 `SSICDR` 分频、理论实际 `div_ssi` 和理论实际 SCLK，
并统计每个 spidev 分块的理论数据周期、实测 `spi_transfer()` 周期、ioctl 内数据占空比、
ioctl 内非数据时间，以及理论数据时间占整个测试的比例。非数据时间包含系统调用、驱动、
调度和 CS 间隙，不能将它理解成纯 CPU ioctl 指令开销；最终 SCLK 必须由示波器确认。

## 连续频率发送

仪器观察 SCLK 与 MOSI 时，使用 `always-speed` 连续发送 `0x55`。在 mode 0、
8-bit、MSB-first 下，MOSI 为重复的 `01010101`。该模式每次发送一条长度等于
检测到的 `spidev.bufsiz` 的 message，不申请 RX 缓冲区，也不进行数据校验，因此
无需短接 MOSI/MISO。

```sh
# 默认 SPI1/PC30 软件 CS，观察 PC25(SCLK)、PC26(MOSI) 与 GND
./app_spi --mode always-speed 60000000 --ssi-source-hz 120000000

# SPI0，观察 PD00(SCLK)、PD01(MOSI) 与 GND
./app_spi --mode always-speed 60000000 --bus 0 --cs pc09 --ssi-source-hz 120000000
```

传输循环内不打印。程序每约两秒输出累计发送字节数、传输次数和有效带宽；按
`Ctrl-C` 正常停止并输出汇总，不显示错误次数。该模式用于观察波形频率，不能替代
外部全双工数据准确性测试。

## CS 时序测量

`delay` 模式在 1MHz、10MHz、60MHz 产生独立采样传输。用仪器测量 SCLK 频率以及
CS 下降沿到首个 SCLK 边沿的时间；程序会记录交互输入的测量值。

```sh
# SPI1 软件 GPIO CS：PC30 -> PC25
./app_spi --mode delay --bus 1 --cs pc30 --ssi-source-hz 120000000

# SPI1 硬件 CE0：PC30/SSI1_CE0 -> PC25
./app_spi --mode delay --bus 1 --hw-cs pc30 --ssi-source-hz 120000000

# SPI0 硬件 CE0：PD05/SSI0_CE0 -> PD00
./app_spi --mode delay --bus 0 --hw-cs pd05 --ssi-source-hz 120000000
```

## 正式资格测试

`--qualification` 是一个自动命令：使用固定软件 CS 配置先测试 SPI0，再测试
SPI1。该命令必须传入 120MHz 源时钟参数，不接受总线或 CS 覆盖参数。

```sh
./app_spi --qualification --ssi-source-hz 120000000 --report /tmp/spi_qualification.md
```

每个 SPI 的执行顺序如下：

1. 扫描外部全双工无错 60MHz 传输。
2. 对全部长度执行短时预检：每个主长度和边界长度各做一次内部 LOOP 校验，随后
   外部随机数据全双工比较至少持续 5 秒。
3. 测量经过数据校验的外部全双工带宽：`16KiB`、`128KiB`、`1MiB` 每档
   1000 个样本，每样本至少 2.5 秒。
4. 测量 `127`、`128`、`129`、`4095`、`4096`、`4097B` 边界；每档 60 个样本，
   每样本至少 5 秒。
5. 执行 64KiB 外部全双工随机数据压力测试 90 分钟，并记录数据校验后的有效带宽。

单路长测部分至少 4 小时 5 分。计入预检和频率扫描，自动完成 SPI0、SPI1 的命令
耗时略高于 8 小时 10 分。一个总线预检失败时，程序跳过该总线的长测，但仍会开始
下一总线，除非操作者中断程序。

Markdown 报告包含每个用例的中位数、最小值、最大值、均值、样本标准差、完成负载
和错误信息。每个独立样本另写入同名 CSV，例如：

```text
/tmp/spi_qualification.md.csv
```

所有外部数据通路都同时使用 TX/RX 缓冲区，并逐字节比较接收数据和发送随机数据。
字节数、传输次数和错误次数均使用 64 位计数，包括 90 分钟压力测试的结果。

## 并发内存与虚拟 framebuffer 压力测试

本次压力场景在 60MHz SPI 独立压力测试期间并发运行两个 `memtester` 和一个 framebuffer
写入任务，用于观察内存压力、CPU 调度和 framebuffer 写入负载下 SPI 外部全双工数据是否
出现中断或校验错误。SPI 测试仍使用本目录的 `app_spi`，不使用 Buildroot 的
`spidev_test`。

该场景不需要接实际 LCD，但目标内核必须启用 `CONFIG_FB_VIRTUAL=y`，并且启动后必须存在
`/dev/fb0`。Buildroot 必须安装 `memtester` 和 `fb-test-app`。其中 `fb-test` 仅写入一次
framebuffer；持续压力应使用无限循环随机写入的 `fb-test-rect`。

每路 SPI 的 MOSI/MISO 仍须按“硬件条件”短接，且 SPI1 的 PC25-PC30 不能被 MSC1 或其他
动态 SPI 设备占用。开始测试前确认不存在遗留的 `/dev/spidev0.*` 或 `/dev/spidev1.*`；如有，
重启并确认设备已释放后再运行测试。

```sh
# 目标机执行；app_spi 已放在 /tmp
mkdir -p /tmp/spi-stress
cd /tmp

ls -l /dev/fb0 /dev/spidev*
which memtester fb-test-rect

# 极限内存压力：按启动压力前的 MemAvailable 计算，预留 5MiB。
# 该值只控制两个 memtester 的申请量；app_spi 运行后的实际剩余内存会继续波动。
avail_kb=$(awk '$1 == "MemAvailable:" { print $2 }' /proc/meminfo)
reserve_mb=5
each_mb=$(( (avail_kb / 1024 - reserve_mb) / 2 ))

echo "MemAvailable=$((avail_kb / 1024)) MiB, each memtester=${each_mb} MiB"
test "$each_mb" -gt 0 || exit 1

memtester "${each_mb}M" 0 >/tmp/spi-stress/memtester-1.log 2>&1 &
mem1=$!

memtester "${each_mb}M" 0 >/tmp/spi-stress/memtester-2.log 2>&1 &
mem2=$!

fb-test-rect -f 0 -s 1 >/tmp/spi-stress/fb-test-rect.log 2>&1 &
fb=$!

cleanup() {
    kill "$mem1" "$mem2" "$fb" 2>/dev/null
    wait "$mem1" "$mem2" "$fb" 2>/dev/null
}

trap cleanup EXIT INT TERM

sleep 2
kill -0 "$mem1" || exit 1
kill -0 "$mem2" || exit 1
kill -0 "$fb" || exit 1

echo "MemAvailable after stress start: $(awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo)"

./app_spi --qualification --ssi-source-hz 120000000 --report /tmp/spi-stress/spi_qualification.md
result=$?

cleanup
trap - EXIT INT TERM
echo "app_spi exit code: $result"
```

`memtester ... 0` 会循环执行。`reserve_mb=5` 是极限压力设置：按整数 MiB 分配时，两个
`memtester` 启动后理论上至少留下约 5MiB，但无法保证在 `app_spi`、shell、内核回收和
framebuffer 继续运行后仍精确剩余 5MiB。上述 `MemAvailable after stress start` 仅作即时
观测；测试期间可重复执行 `awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo`
查看实际可用内存。

该设置允许 OOM 作为压力现象被记录，但只要出现 `Out of memory` 或 `Killed process`，该轮
SPI 结果只能说明极限内存耗尽时的行为，不能作为“60MHz SPI 稳定”的通过结论。若要获得有效的
稳定性结论，恢复 `reserve_mb=64`；若第二个 `memtester` 在 5MiB 设置下无法启动，说明系统
启动开销已超过可承受范围，应调大 `reserve_mb`，而不是强行继续。

测试结束后检查 `/tmp/spi-stress/spi1_stress.md`、两个 `memtester` 日志、
`fb-test-rect.log` 和 `dmesg`。SPI 报告的“失败”和“数据错误”必须为零；`dmesg` 中不能出现
`Out of memory`、`Killed process` 或 SPI 超时。

## 独立压力测试

`--stress` 只执行 60MHz、64KiB 随机数据的外部全双工长期校验，不执行频率扫描、边界长度
测试或 SPI0/SPI1 自动切换。默认持续 5400 秒（90 分钟），可用 `--stress-seconds` 自定义。
`--bus 0` 默认使用软件 CS `pc09`，`--bus 1` 默认使用 `pc30`；可用 `--cs` 覆盖默认 CS。
测试每 10 秒在终端输出进度，首次传输错误或按 `Ctrl-C` 停止时写入当前报告。

```sh
# SPI1，默认 90 分钟
./app_spi --stress --bus 1 --report /tmp/spi1_stress.md

# SPI0，运行 10 分钟
./app_spi --stress --bus 0 --stress-seconds 600 --report /tmp/spi0_stress.md
```
