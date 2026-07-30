# X2600 EVB 2.0 SPI1 性能测试

本目录测试 X2600 的 **SPI1 PC25 引脚组**。当前默认总线为 1。

## Kconfig 与接线

在工程 Kconfig 中启用 SPI1，并选择 PC25 组：

```text
MD_X2600_510_SPI=y
MD_X2600_510_SPI1=y
MD_X2600_510_SPI1_CLK=PC25
MD_X2600_510_SPI1_MOSI=PC26
MD_X2600_510_SPI1_MISO=PC27
```

PC25-PC30 同时复用为 MSC1。测试期间应关闭 `MD_X2600_510_MSC` 或至少 `MD_X2600_510_MSC1`，并确认系统未加载 `soc_msc`。当前 `x2600_nor_5.10_defconfig` 已关闭整个 MSC 模块。

EVB 2.0 的 CON11 引脚：

| 信号 | X2600 引脚 | CON11 引脚 | 用途 |
|---|---|---:|---|
| SCLK | PC25 | 2 | SPI1 时钟、仪器测量 |
| MISO | PC27 | 3 | 外部回环接收 |
| MOSI | PC26 | 4 | 外部回环发送 |
| CS | PC30 | 6 | 软件 GPIO CS 或硬件 SSI1_CE0 |

将 **PC26(MOSI) 与 PC27(MISO) 短接**，并让仪器和 EVB 共地。软件 CS 与硬件 CS 都使用 PC30：程序先将其作为 GPIO 软件 CS，硬件阶段会注销软件 CS 设备，再把 PC30 切换为 `func1` 的 `SSI1_CE0`。

## 完整测试

已确认 SSI 源时钟为 96 MHz，最高实际 SCLK 是 48 MHz：

```sh
make app
adb push app_spi /tmp/app_spi
adb shell
cd /tmp
chmod 755 app_spi
./app_spi --mode complete --ssi-source-hz 96000000 --bus 1 \
  --cs pc30 --hw-cs pc30 --report /tmp/spi1_complete.md
```

完整模式执行：

1. PC26-PC27 外部回环扫描，确认最高无错频率。
2. 48 MHz 下的 `SPI_LOOP`、仅写、仅读、外部全双工随机数据性能；数据长度为 16 KiB、64 KiB、256 KiB、1 MiB。
3. PC30 软件 GPIO CS 到 PC25 首个 SCLK 的 1/10/48 MHz 时序。
4. PC30 硬件 SSI1_CE0 到 PC25 首个 SCLK 的 1/10/48 MHz 时序。

`SPI_LOOP` 不输出有效引脚波形。仅写路径物理上会接收 MISO，但驱动丢弃 RX FIFO 数据，因此不校验；仅读以虚拟 0 产生时钟；外部全双工逐字节校验随机 TX/RX 数据。

## 单独 CS 时序

```sh
# 软件 CS：PC30 -> PC25，依次触发 1 / 10 / 48 MHz
./app_spi --mode delay --ssi-source-hz 96000000 --bus 1 --cs pc30

# 硬件 CS：PC30/SSI1_CE0 -> PC25，依次触发 1 / 10 / 48 MHz
./app_spi --mode delay --ssi-source-hz 96000000 --bus 1 --hw-cs pc30
```

每档会先执行一次未触发的短传输，释放可能被旧驱动保留的 CS；正式采样传输使用 `cs_change=1`，结束后明确释放 CS。因此每档应有独立的 CS 高-低-高波形。

## 连续 SCLK 与 MOSI 图样

```sh
/module_driver/app_spi --freq 48000000 --bus 1 --report /tmp/spi1_freq.md
```

该模式不执行性能测试，使用 `soc_spi` 的 TX/RX 循环 DMA 持续发送 `0x55` 并持续回收 RX FIFO。在 MSB-first、mode 0 下，PC26(MOSI) 输出重复的 `01010101`，PC25 持续输出请求 SCLK；按 Ctrl-C 停止。它不经过 spidev 分块 ioctl，因此没有用户态块边界造成的 SCLK 间隙。

`--freq 80000000` 仅请求 80 MHz；在 96 MHz SSI 源时钟下实际会受分频限制为 48 MHz。观察 48 MHz 波形时直接使用 `--freq 48000000`。

构建工程并烧录 rootfs 后，程序安装在 `/module_driver/app_spi`，正常重启不会清空。`/tmp` 是临时文件系统，会在重启后清空；`buildroot/output/target` 是主机的 rootfs 构建暂存目录，重新构建时也可能被重建。

24 MS/s 逻辑分析仪能确认 CS 是否翻转，也能粗略观察低速时钟，但无法可靠量 48 MHz 周期或几十纳秒的 CS 时序。48 MHz 建议使用至少 500 MS/s 的逻辑分析仪，或 200 MHz 带宽、1 GS/s 采样率的示波器。
