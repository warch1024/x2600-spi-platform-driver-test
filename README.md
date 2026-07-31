# X2600 SPI 60MHz 测试

`app_spi` 只保留 CS 时序测量、快速完整测试和 60MHz 正式资格测试。

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
