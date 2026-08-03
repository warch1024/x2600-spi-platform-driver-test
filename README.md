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
动态 SPI 设备占用。`app_spi` 会动态注册并在正常退出时注销 spidev；异常停止后可能遗留
`/dev/spidev1.0`。不要使用 `rm` 删除设备节点。对于 SPI1/PC30 测试，在确认没有其他程序使用
该设备后，使用 `cmd_spi` 通过 `/dev/spidev_helper` 注销：

```sh
cmd_spi del_dev /dev/spidev1.0
test ! -e /dev/spidev1.0 || exit 1
```

`cmd_spi` 必须存在；当前 `x2600_nor_5.10_defconfig` 已启用 `APP_libhardware2_spi_cmd`。若命令
缺失或注销失败，停止测试并重启后确认节点已释放。SPI0 测试的遗留节点使用相同方法替换为
`/dev/spidev0.0`。

```sh
# 目标机执行；app_spi 已放在 /tmp
mkdir -p /tmp/spi-stress
cd /tmp

ls -l /dev/fb0 /dev/spidev*
which memtester fb-test-rect cmd_spi

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
dmesg > /tmp/spi-stress/dmesg.log

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

测试结束后检查 `/tmp/spi-stress/spi_qualification.md`、两个 `memtester` 日志、
`fb-test-rect.log` 和 `dmesg`。SPI 报告的“失败”和“数据错误”必须为零；`dmesg` 中不能出现
`Out of memory`、`Killed process` 或 SPI 超时。

## 临界压力与引脚波形测试

资格测试证明某一压力等级下端到端随机数据正确。下表用于首次建立某种负载的压力边界、增加或
替换后台负载后重新定位，或已经出现异常时缩小复现范围。先在无后台任务时建立基线，再在每个
等级启动两个 `memtester`、一个 `fb-test-rect` 后运行 SPI1 的 600 秒独立压力测试。每级至少
重复三次；某级首次出现数据错误、SPI 返回错误或吞吐量异常下降时，不再提升压力，保留该等级
复现和采集波形。

| 等级 | `reserve_mb` | 目的 |
|---|---:|---|
| 0 | 不启动后台任务 | SPI 基线 |
| 1 | 64 | 轻度内存压力 |
| 2 | 32 | 中度内存压力 |
| 3 | 16 | 重度内存压力 |
| 4 | 5 | 极限内存压力 |

若同一固件、同一 SPI 配置和同一组后台任务已经在等级 4 完整资格测试通过，则该组负载的更低
等级无需再补跑；等级 4 已覆盖这组任务可达到的最高内存压力。此时要继续探索 SPI 的临界条件，
应新增实际业务负载，例如真实 LCD/DPU、视频编解码、摄像头、网络或存储，而不是重复较轻的
内存等级。

等级 1 至 4 使用上一节的启动脚本，只修改 `reserve_mb`，并将资格测试命令替换为：

```sh
./app_spi --stress --bus 1 --stress-seconds 600 --report /tmp/spi-stress/spi1_level.md
```

每级完成后检查报告的“失败”和“数据错误”是否为零，并保存两个 `memtester` 日志、
`fb-test-rect.log` 和 `dmesg`。`reserve_mb` 不能低于 5；更低的值主要触发 OOM，不能用于判断
SPI 的稳定性临界点。找到最高无错等级后，再以该等级运行完整资格测试。

`--stress` 在当前 `spidev.bufsiz=4096B` 时，会将每个 64KiB 随机缓冲区分为多个 SPI message。
每个 4KiB message 在 60MHz 下的纯数据时间约为 546us。`app_spi` 将 `cs_change` 设为 0，驱动
会在同一设备的相邻 message 间保持软件 CS 有效；但每个 4KiB DMA 传输完成时 FIFO 为空，硬件
传输结束，下一段须经 ioctl 和 DMA 重新提交。因此 SCLK 在约 4KiB 边界处停止，即使 CS 仍有效，
也属于当前传输路径的正常分段行为，不能直接当作异常。若要确认“有 SCLK 但 MOSI 无数据”或异常
空白，必须在复现压力等级下用示波器或逻辑分析仪同时采集 SPI1 的 PC25(SCLK)、PC26(MOSI)、
PC30(CS) 和 GND，并运行：

```sh
./app_spi --mode always-speed 60000000 --bus 1 --cs pc30 --ssi-source-hz 120000000
```

该模式连续发送 `0x55`，MOSI 应为重复的 `01010101`，便于观察时钟连续时 MOSI 是否停滞或异常。
它不做 RX 数据校验，必须与上述 `--stress` 随机全双工校验分开运行。约 4KiB 边界处的 SCLK 空档
是当前路径的正常现象；单个 4KiB message 内出现空档、SCLK 连续而 MOSI 不符合 `0x55`、内核出现
`SPI%d transmit underrun`，或 `--stress` 报告随机数据错误，才是需要结合波形和 `dmesg` 进一步
定位的异常。

### MOSI 波形对照场景

两个场景都使用上一节的两个 `memtester`、`fb-test-rect` 和 `reserve_mb=5`，保持约 5MiB 剩余
内存。示波器 CH1 接 PC25(SCLK)、CH2 接 PC26(MOSI)、CH3 接 PC30(CS)，探头地接开发板 GND。
使用 10x 探头，采样率至少 500MSa/s、带宽建议 200MHz 以上；以 CS 下降沿触发，记录窗口至少 1ms，
并开启余辉或分段采集。放大至约 20ns/div 后检查单 bit 的 MOSI。

场景 A 用于建立内存和虚拟 framebuffer 压力下的基线。启动上一节全部后台任务并完成存活检查后，
将资格测试命令替换为以下命令，采集至少 30 秒后按 `Ctrl-C`：

```sh
./app_spi --mode always-speed 60000000 --bus 1 --cs pc30 --ssi-source-hz 120000000 --report /tmp/spi-stress/spi1_wave_memfb.md
```

场景 B 只在场景 A 完成并清理全部后台任务后执行。重新启动同一组后台任务，但先将
`reserve_mb` 设为 16，再在 `fb-test-rect` 启动后增加一个 CPU 调度竞争进程：

```sh
yes >/dev/null 2>&1 &
cpu=$!
sleep 1
kill -0 "$cpu" || exit 1
```

将 `cpu` 加入清理函数：

```sh
cleanup() {
    kill "$mem1" "$mem2" "$fb" "$cpu" 2>/dev/null
    wait "$mem1" "$mem2" "$fb" "$cpu" 2>/dev/null
}
```

然后运行同一波形命令，并使用不同报告文件：

```sh
./app_spi --mode always-speed 60000000 --bus 1 --cs pc30 --ssi-source-hz 120000000 --report /tmp/spi-stress/spi1_wave_memfb_cpu.md
```

每轮结束后、清理后台任务前导出内核日志：

```sh
dmesg > /tmp/spi-stress/dmesg.log
```

比较两个场景的约 4KiB 数据段间空档宽度和抖动。两个 `memtester` 已提供 CPU/DDR 负载；场景 B 的
`yes` 仅增加调度竞争。若场景 B 波形无变化，不应继续降低 `reserve_mb`，而应改用真实 LCD/DPU、
视频编解码、摄像头、网络或 USB 等 DMA/中断负载。两个场景完成后，应在同样的后台负载下重新运行
`--stress` 随机全双工测试，确认波形观察到的异常是否同时造成数据校验错误。

场景 B 不应在 `reserve_mb=5` 下直接执行。该组合可能使 DMA 引擎无法分配软件描述符；当前 SPI
驱动在 `device_prep_slave_sg()` 返回空描述符时调用 `BUG()`，会触发内核异常，而不是将错误返回给
`app_spi`。在驱动改为可恢复的 `-ENOMEM` 错误处理前，CPU 竞争场景从 `reserve_mb=16` 开始；需要
继续降低内存时，每次只降低一个等级，并在每轮后检查 `dmesg`。

### 串口前台与 ADB 递增加压

该流程用于在示波器持续观察 MOSI 时逐项加入负载，定位首个导致波形异常的压力源。串口前台只
运行 `app_spi`；ADB shell 只启动和停止后台任务。开始前处理遗留 `/dev/spidev1.0`，然后在串口
终端运行：

```sh
cd /tmp
./app_spi --mode always-speed 60000000 --bus 1 --cs pc30 --ssi-source-hz 120000000 --report /tmp/spi-stress/wave-live.md
```

示波器连接 PC25(SCLK)、PC26(MOSI)、PC30(CS) 与 GND。约 4KiB 数据段的 SCLK 空档是当前 DMA
分段的正常现象；单个 4KiB 段内部出现空档，或 SCLK 连续时 MOSI 不再是 `01010101`，才是异常。

在 ADB shell 中创建日志目录后，每次只增加一个后台任务，每级观察 20 至 30 秒并记录示波器结果：

```sh
cd /tmp
mkdir -p /tmp/spi-stress

memtester 8M 0 >/tmp/spi-stress/memtester-1.log 2>&1 &
mem1=$!
sleep 2
kill -0 "$mem1"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo
```

再依次执行：

```sh
memtester 8M 0 >/tmp/spi-stress/memtester-2.log 2>&1 &
mem2=$!
sleep 2
kill -0 "$mem2"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo
```

```sh
fb-test-rect -f 0 -s 1 >/tmp/spi-stress/fb-test-rect.log 2>&1 &
fb=$!
sleep 2
kill -0 "$fb"
```

```sh
yes >/dev/null 2>&1 &
cpu=$!
sleep 1
kill -0 "$cpu"
```

需要继续增加内存时，每次增加一个 8MiB `memtester`，例如：

```sh
memtester 8M 0 >/tmp/spi-stress/memtester-3.log 2>&1 &
mem3=$!
sleep 2
kill -0 "$mem3"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo
```

每级检查串口中的 `SPI: failed to transfer`，并在 ADB shell 执行 `dmesg` 检查
`SPI1 transmit underrun`、DMA 错误和 `page allocation failure`。出现首个异常时停止继续加压，立即
保存：

```sh
dmesg > /tmp/spi-stress/dmesg.log
```

停止串口中的 `always-speed` 后，在 ADB shell 清理全部后台任务：

```sh
kill "$mem1" "$mem2" "$mem3" "$fb" "$cpu" 2>/dev/null
wait "$mem1" "$mem2" "$mem3" "$fb" "$cpu" 2>/dev/null
```

按 `Ctrl-C` 停止 `always-speed` 时，当前驱动可能将信号中断误报为 `rx transfer timeout`；该信息不能
单独判定为 MOSI 数据断开，必须结合停止前的波形和 `dmesg` 判断。

虚拟 framebuffer 只产生 CPU 和内存写压力，不覆盖真实 LCD/DPU 的 DMA、中断和显示链路。最终的
真实业务验证应将该任务替换为产品实际使用的显示、编解码、网络或存储负载。

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
