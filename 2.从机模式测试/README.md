# X2600 SPI 主从功能测试

本目录提供两块 X2600 板之间的 SPI 主从功能测试，不包含或引用 `1.超频-压力-波形测试` 中的文件。

- NAND 板使用 SPI1 作为主机。
- NOR 板使用 SSLV0 作为从机。
- 主机默认请求 1MHz SCLK；可用 `--speed` 调整请求频率，SSLV 手册规定上限为 40MHz。
- 当前 SSLV 驱动只接受 SPI mode 3，因此主机和从机固定为 mode 3、MSB-first。

## X2600 SSLV 默认 SPI 属性

当前 `x2600_510/sslv` 驱动的默认配置和限制如下：

| 属性 | 当前设置或限制 |
|---|---|
| 工作角色 | SPI 从机（SSLV0） |
| 帧格式 | Motorola SPI 标准帧格式 |
| SPI mode | 只支持 Mode 3：`CPOL=1`、`CPHA=1`；Mode 0/1/2 会被驱动拒绝 |
| 传输模式 | `transfer_mode=0`，收发同时进行（全双工） |
| 默认 `bpw` | 8 bit |
| `bpw` 范围 | 4-32 bit |
| 字节序配置 | 驱动没有大端/小端切换接口，也没有 LSB-first 配置接口 |

`bpw` 决定驱动访问用户缓冲区时使用的基本类型：

| `bpw` 范围 | 每个 word 的缓冲区访问宽度 |
|---|---:|
| 4-8 | 1 byte |
| 9-16 | 2 bytes（按 `u16` 访问） |
| 17-32 | 4 bytes（按 `u32` 访问） |

因此，收发缓冲区长度必须是上述访问宽度的整数倍。对于 16/32-bit word，驱动直接按 CPU 原生字节序读写 `u16/u32`，不会自动做字节交换；如果协议要求固定的大端字节流，应由测试程序或调用方提前组包，接收后再按协议解析。SPI 线上采用 MSB-first 是本测试双方的固定约定，并非 SSLV 驱动提供的可选字节序参数。

## 接线

| NAND 主机 | NOR 从机 |
|---|---|
| PC15 SPI1 CLK | PB28 SSLV CLK |
| PC16 SPI1 MOSI | PB30 SSLV DR |
| PC17 SPI1 MISO | PB29 SSLV DT |
| PC14 软件 CS | PB31 SSLV CS |
| GND | GND |

`--direction rx` 验证 MOSI 到从机 DR；`--direction tx` 验证从机 DT 到主机 MISO；
`--direction duplex` 同时验证两个方向。

## 测试原理

主机为每个 word 生成预先定义的非对称数值，并按高字节在前的规范字节流放入 spidev 发送缓冲区。例如，
16-bit 的 `0x1234` 对应字节 `12 34`，32-bit 的 `0x12345678` 对应 `12 34 56 78`。

从机以相同位宽接收原始字节，然后与同一份规范字节流逐字节比较。图样中包含 `0x12`、`0x34`、`0x81`、`0x7e`、
`0x1234`、`0xa55a`、`0x12345678` 等非对称值，因此字节交换和多数 bit 反转不会被相同数值掩盖。

每个 case 还带有递增序号；主机和从机 TX 使用不同的图样流。这样同一位宽的某一帧被漏掉或重复时，也会报告
`mismatch`，不会被重复的固定图样掩盖。序号为 0 的主机图样保持原有字节序测试图样。

比较结果的含义如下：

| 结果 | 判定方法 | 表示的问题 |
|---|---|---|
| `match` | 收到的每个字节与规范字节流完全一致 | 当前位宽、mode 3、MSB-first 和本次图样下，主机到从机未出现可见的字节序或 bit 序变化。 |
| `word-byte-swap` | 每个 16-bit 或 32-bit word 内的字节顺序完全反转 | 存在典型的多字节大小端交换，例如 `12 34` 变为 `34 12`。 |
| `bit-reverse` | 每一个字节的 bit 顺序完全反转 | 存在 MSB/LSB bit 序不一致，例如 `0x12` 变为 `0x48`。 |
| `word-byte-swap+bit-reverse` | 每个 word 先字节反转，再对每个字节 bit 反转 | 同时存在上述两种固定转换。 |
| `mismatch` | 不属于以上固定转换 | 数据错误、部分交换、word 对齐错误、时序采样错误或其它未分类转换；程序会打印期望值与实收值。 |

### 8-bit、16-bit、32-bit 分别检测什么

| 位宽 | 检测重点 | 能发现的问题 |
|---|---|---|
| 8-bit | 单字节内容与 bit 顺序 | 8-bit 数据损坏、bit 反转、错位导致的非预期值。单个字节不存在多字节大小端问题。 |
| 16-bit | 每个 2-byte word 的顺序与 bit 顺序 | `0x1234` 变成 `0x3412` 的 16-bit 字节交换，以及 bit 反转或一般错误。 |
| 32-bit | 每个 4-byte word 的顺序与 bit 顺序 | `0x12345678` 变成 `0x78563412` 的 32-bit 全字节交换，以及 bit 反转或一般错误。 |

每一阶段默认发送 8 个 word。`--words N` 支持 1 至 4096 word；`--loops N` 会重复指定阶段，主从两端必须使用
相同的循环次数、位宽、word 数和方向。`tx`、`duplex` 限制为不超过 64 word，因为现有 SSLV 用户态接口不能同时
完成 TX FIFO 续填和 RX arm。

## 覆盖范围

这不是完整的 SPI 资格测试，`match` 不能证明所有 SPI 功能均正常。当前程序可以执行以下项目：

| 项目 | 接口 | 覆盖内容 |
|---|---|---|
| 配置负测试 | 从机 `--config-check` | mode 0/1/2、bit 3/33、busy 状态下重新配置均应失败 |
| 基础接收 | 两端 `--direction rx` | MOSI 到 DR、8/16/32 bit、序号和字节序 |
| 帧边界压力 | `--loops`、`--case-delay-ms` | 包含 32-bit 到下一轮 8-bit 的延时与序号错帧检测 |
| 长接收 | `--direction rx --words 65/128/256...` | RX FIFO 阈值、中断多次取数、缓冲区推进 |
| 从机发送 | 两端 `--direction tx` | 从机 DT 到主机 MISO 图样校验 |
| 全双工 | 两端 `--direction duplex` | 主机 MOSI 和从机 MISO 在同一 SPI message 内分别校验 |
| 速度扫描 | 主机 `--speed` | 在 100kHz 至 40MHz 的稳定性扫描 |

以下功能不在当前用户态程序的直接覆盖范围内：

- mode 0/1/2、LSB-first；当前 SSLV 驱动本身拒绝非 mode 3。
- SSP、Microwire、DMA、内核 callback 接收和硬件 loopback；HAL 有相应定义，但当前 SSLV 用户接口没有配置入口。
- RX overflow 的可靠错误返回；当前驱动未在 IRQ handler 中转换为接收错误。
- `close()`、进程异常退出后的硬件自动清理；当前 `release()` 尚未实现清理。
- CS 建立/保持时间、SCLK 占空比、信号完整性和电平问题；这些需要示波器或逻辑分析仪配合验证。

如需扩大覆盖，应增加从机预装载回传数据后的全双工比较、随机图样、长度和频率扫描，以及波形采集。

## 构建

```sh
make test
make
```

`spi_endian_master` 只使用 Linux spidev ABI，不依赖 `spi_add_device`。`spi_endian_slave` 链接 SSLV
公共库，只能在 NOR 从机镜像上运行。

## 运行

NAND 板先加载 `soc_spi.ko` 和 `spidev_helper.ko`，再创建带软件 CS 的 SPI1 设备。当前
`x2600h_nand_5.10_defconfig` 及其基础 DTS 未占用 PC14，可将其作为连接 NOR PB31 的软件 CS。

```sh
cmd_spi add_dev 1 PC14
```

NOR 板必须先运行从机，使其准备接收第一组数据：

```sh
./spi_endian_slave --device /dev/sslv0 --direction rx --words 8 --loops 1
```

随后在 NAND 板运行主机：

```sh
./spi_endian_master --device /dev/spidev1.0 --speed 1000000 --direction rx --words 8 --loops 1
```

从机输出三个阶段的判定结果；只有三个阶段均为 `match` 时，从机程序才返回 0。

主机程序会在每个 8/16/32-bit 用例结束时显式释放软件 CS；抓 PC14 时，空闲应为高电平，运行一次
`--loops 1` 应看到三段低电平 CS 脉冲。主机输出中的 `cs released` 表示对应 ioctl 已完成且返回长度
符合预期，但不代表从机已收到数据。确认功能后再逐级提高 `--speed`，最大不要超过 `40000000`。

默认情况下，主机在每个相邻 case 之间等待 100 ms，包括 32-bit 到下一轮 8-bit，给从机完成关闭、修改 `bpw` 和
重新使能。可用 `--case-delay-ms N` 调整等待时间，`--start-delay-ms N` 可在启动后再等待 N ms。固定等待只用于
压力筛选，不能替代 READY/ACK 握手。排查时应先让主从仅运行同一位宽，例如：

```sh
# NOR 从机
./spi_endian_slave --device /dev/sslv0 --bits 8 --loops 1

# NAND 主机
./spi_endian_master --device /dev/spidev1.0 --speed 1000000 --bits 8 --loops 1
```

`--bits` 只接受 8、16、32。单独位宽稳定后，再去掉 `--bits` 运行完整测试。

## 其他用例

配置负测试不需要主机：

```sh
./spi_endian_slave --device /dev/sslv0 --config-check
```

长接收用例先从低速开始：

```sh
# NOR slave
./spi_endian_slave --bits 16 --words 128 --direction rx --loops 10

# NAND master
./spi_endian_master --bits 16 --words 128 --direction rx --speed 1000000 --loops 10
```

从机发送和全双工均要求双方使用相同方向及不超过 64 word。主机在 MISO 侧给出比较结果；duplex 模式下从机还会
校验 MOSI：

```sh
# slave: tx 或 duplex
./spi_endian_slave --bits 16 --words 8 --direction duplex --loops 1

# master
./spi_endian_master --bits 16 --words 8 --direction duplex --speed 1000000 --loops 1
```

## 查看内核日志

当前工作树中的 SSLV 驱动没有 `debug` 模块参数；部分 BusyBox 的 `dmesg` 也不支持 `-w`。排查时可在测试前清空
旧日志，完成一轮后读取新的 SSLV 日志：

```sh
dmesg -c >/dev/null
# 运行一轮主从测试
dmesg | grep SSLV
```
