# X2600 SPI 主从功能测试

本目录提供两块 X2600 板之间的 SPI 主从功能测试程序和测试记录。测试目标是验证 NAND 板的 SPI1 主机与 NOR 板的 SSLV0 从机，在当前驱动支持的配置下能否正确完成配置回读、收发、位宽切换、重复 arm、长帧接收、从机回传、TX FIFO 边界、尾随零 word 和全双工传输。

本文档描述的是当前 `spi_endian_master`、`spi_endian_slave` 和 `pattern.c` 的实际行为，不包含 `1.超频-压力-波形测试` 中的内容。

## 1. 测试边界

### 1.1 硬件角色和设备节点

| 角色 | 板端 | 控制器 | 设备节点 | 程序 |
|---|---|---|---|---|
| 主机 | NAND 板 | SPI1 | `/dev/spidev1.0` | `spi_endian_master` |
| 从机 | NOR 板 | SSLV0 | `/dev/sslv0` | `spi_endian_slave` |

主机产生 SCLK 和 CS，并通过 MOSI 发送主机图样。SPI 是时钟驱动的全双工总线，即使测试方向写为 `tx`，主机仍要发送时钟和填充数据，才能把从机 DT 上的数据读回。

### 1.2 接线

| NAND 主机 | NOR 从机 | 信号方向 |
|---|---|---|
| PC15 SPI1 CLK | PB28 SSLV CLK | 主机到从机 |
| PC16 SPI1 MOSI | PB30 SSLV DR | 主机到从机 |
| PC17 SPI1 MISO | PB29 SSLV DT | 从机到主机 |
| PC14 软件 CS | PB31 SSLV CS | 主机到从机 |
| GND | GND | 必须共地 |

没有共地或 CS、CLK、MOSI、MISO 接错时，低速也可能出现随机 `mismatch`；频率升高后通常更容易复现。

### 1.3 固定协议契约

| 属性 | 当前程序/驱动行为 |
|---|---|
| 帧格式 | Motorola SPI 标准帧格式 |
| SPI mode | 固定 mode 3：`CPOL=1`、`CPHA=1` |
| bit order | 固定 MSB-first |
| SSLV 传输模式 | `transfer_mode=0`，硬件同时收发 |
| 测试位宽 | 8、16、32 bit |
| 单次最大 word 数 | `--words` 支持 1 至 4096 |
| `tx` / `duplex` 最大 word 数 | 64；现有 SSLV 用户接口不支持 TX FIFO 与 RX arm 的无限续填 |

从机驱动会拒绝 mode 0、1、2。因此 `--direction rx`、`tx`、`duplex` 是本测试程序的校验方向，不是切换 SSLV 硬件 SPI mode 或硬件半双工模式。

## 2. 数据图样和判定逻辑

### 2.1 规范字节流

程序不直接比较主机或从机 CPU 中的 `u16` / `u32` 数值，而是生成高字节在前的规范字节流。基础图样包含非对称值，避免字节交换或 bit 反转被对称数据掩盖。

| 位宽 | 8 个基础 word |
|---|---|
| 8 bit | `12 34 a5 5a 81 7e 01 fe` |
| 16 bit | `1234 a55a 8001 0f70 5aa5 7e81 0102 fe7d` |
| 32 bit | `12345678 a55ac33c 8001f00d 0f1e2d3c 5aa5c33c 7e81b42d 01020304 fe7d6c5b` |

例如，16-bit word `0x1234` 在线上和比较缓冲区中的规范表示都是 `12 34`；32-bit word `0x12345678` 表示为 `12 34 56 78`。

### 2.2 序号和双向图样

每个 case 有单调递增的 `sequence`。主机图样使用：

```text
base_word XOR (sequence * 0x9e3779b9)
```

从机 TX 图样在此基础上再使用独立的从机流掩码 `0xa55a3cc3`。位宽为 8 或 16 时只取掩码低 8 或低 16 bit。

这两个设计有两个目的：

1. 同一位宽的相邻帧内容不同，漏帧、重帧或主从 case 顺序不一致不会被固定图样掩盖。
2. 全双工时，主机 MOSI 图样和从机 MISO 图样不同，可分别确认两个方向没有误接或错误复用。

### 2.3 比较结果的含义

| 输出结果 | 程序判定 | 优先排查方向 |
|---|---|---|
| `match` | 每个实收字节均与当前 sequence 的期望字节一致 | 当前 case 通过 |
| `word-byte-swap` | 每个 16/32-bit word 内的字节完全反序 | 16/32-bit 缓冲区字节序、驱动 word 访问或接收端解析 |
| `bit-reverse` | 每个字节 bit 顺序完全反转 | MSB/LSB-first 不一致、采样/协议配置错误 |
| `word-byte-swap+bit-reverse` | 同时满足 word 内字节反序和逐字节 bit 反转 | bit order 与字节序均不一致 |
| `mismatch` | 不属于上述固定变换 | 数据损坏、帧错位、残留 FIFO、CS/SCLK 时序或信号完整性问题 |

对于 `mismatch`，从机会打印 `expected:` 和 `actual:`。不要只看第一处不同：如果实际数据开头正好等于上一 case 的图样，优先怀疑首帧丢失或 RX FIFO 残留，而不是大小端。

## 3. 命令接口

### 3.1 主机

```text
spi_endian_master [--device PATH] [--speed HZ] [--loops N]
                  [--bits 8|16|32] [--words N]
                  [--direction rx|tx|duplex]
                  [--start-delay-ms N] [--case-delay-ms N]
                  [--suite] [--log PATH]
```

| 参数 | 默认值 | 作用 |
|---|---:|---|
| `--device` | `/dev/spidev1.0` | spidev 设备节点 |
| `--speed` | `1000000` | 请求的单次传输 SCLK 频率，范围 1 至 40000000 Hz |
| `--loops` | `1` | 手工模式下的循环次数 |
| `--bits` | 未指定，依次测 8/16/32 | 仅测试一个位宽 |
| `--words` | `8` | 每个 case 的 word 数 |
| `--direction` | `rx` | `rx`、`tx` 或 `duplex` |
| `--start-delay-ms` | `0` | 程序启动后、第一帧前的延时 |
| `--case-delay-ms` | `100` | 相邻 case 之间的主机延时；suite 也使用此参数 |
| `--suite` | 关闭 | 执行固定的完整测试序列 |
| `--log` | app 所在目录的时间戳文件 | 指定 suite Markdown 报告路径 |

主机打印的 `sent; cs released` 表示 `SPI_IOC_MESSAGE` 返回了完整长度且本 case 的软件 CS 已释放；它不单独证明从机已收到数据。`tx` 和 `duplex` 模式下，主机还会输出 `miso=match` 或 MISO 的期望/实际数据。

### 3.2 从机

```text
spi_endian_slave [--device PATH] [--loops N] [--bits 8|16|32]
                 [--words N] [--direction rx|tx|duplex]
                 [--config-check] [--suite] [--log PATH]
```

| 参数 | 默认值 | 作用 |
|---|---:|---|
| `--device` | `/dev/sslv0` | SSLV 字符设备节点 |
| `--loops` | `1` | 手工模式下的循环次数 |
| `--bits` | 未指定，依次测 8/16/32 | 仅测试一个位宽 |
| `--words` | `8` | 每个 case 的 word 数 |
| `--direction` | `rx` | `rx`、`tx` 或 `duplex` |
| `--config-check` | 关闭 | 只执行从机配置负测试，不需要主机 |
| `--suite` | 关闭 | 执行固定的完整测试序列 |
| `--log` | app 所在目录的时间戳文件 | 指定 suite Markdown 报告路径 |

从机在每个 case 先设置位宽、使能 SSLV、必要时预装载 TX 图样，再打印 `ready; start master now`，随后阻塞在接收。普通手工模式没有应用层超时；suite 模式对每个接收 case 设置 15 秒超时，避免主机未启动时永久阻塞。

## 4. 完整 suite

### 4.1 执行顺序

完整 suite 必须先启动从机，再启动主机。主从的 case 编号、方向、位宽和 word 数严格对应。

```sh
# NOR 从机板：先运行
./spi_endian_slave --device /dev/sslv0 \
    --suite --log /tmp/slave_suite.md

# NAND 主机板：从机出现 wait for master suite 后立即运行
./spi_endian_master --device /dev/spidev1.0 \
    --speed 1000000 --suite --log /tmp/master_suite.md
```

从机首个接收 case 的 suite 超时为 15 秒，因此不要在从机已输出等待信息后长时间再启动主机。两端任一 suite 最终返回 `0` 表示全部检查通过；返回 `1` 表示至少一个检查失败。每组失败后程序仍会继续执行后续组，以保留更多定位信息。

报告默认在执行 app 的目录生成。例如从 `/tmp` 执行 `./spi_endian_slave` 时，默认报告在 `/tmp`；建议使用 `--log` 固定写入可回收路径。设备未校时可能使自动文件名显示为 `19700101`，这不影响数据判定。

### 4.2 suite 用例清单

固定 suite 有 44 个数据传输 case。汇总输出有 15 项：配置组、8 个功能组、5 个速度子项和 overall。概念上为 10 组，其中第 10 组包含 5 个频率点。

| 组 | case | 参数 | 方向 | 验证内容 | 通过条件 |
|---|---:|---|---|---|---|
| `01_config` | 无数据帧 | mode 0/1/2、bpw 3/33/4/32、busy 时改 mode/bpw、`get_info` | 从机；主机检查 mode 3/bpw 8 配置 | 参数约束、配置回读和 busy 状态保护 | 非法配置均被拒绝；4/8/32 bit 回读正确；enable/disable 前后回读一致 |
| `02_basic_rx` | 0-2 | 8/16/32 bit，各 8 word，1 轮 | RX | 三种位宽的基础 MOSI 到 DR 接收和字节流 | 从机三个 case 都为 `match` |
| `03_rearm` | 3-17 | 8/16/32 bit，各 8 word，5 轮 | RX | disable/set bpw/enable/receive 的重复切换，15 次连续 arm | 15 个 case 都为 `match`，sequence 连续 |
| `04_long_rx` | 18-23 | 8/16/32 bit，各 128 word，2 轮 | RX | 三种位宽的多阈值 RX FIFO 读取、中断唤醒、缓冲区指针推进 | 6 帧均完整匹配 |
| `05_slave_tx_widths` | 24-26 | 8/16/32 bit，各 8 word | TX | 三种位宽的从机 TX FIFO、DT 到主机 MISO | 主机 3 个 case 均为 `miso=match` |
| `06_slave_tx_min` | 27-29 | 8/16/32 bit，各 1 word | TX | 最小 TX 长度和单 word 打包 | 主机 3 个 case 均为 `miso=match` |
| `07_slave_tx_fifo_limit` | 30-32 | 8/16/32 bit，各 64 word | TX | 64-entry TX FIFO 初始满载边界 | 主机 3 个 case 均为 `miso=match` |
| `08_slave_tx_append_zero` | 33-35 | 8/16/32 bit，8 payload word + 1 zero word | TX | `CMD_send(add_zero=1)` 的尾随零 word 和返回长度 | 主机 MISO 的第 9 word 为零；从机 `send` 返回 payload byte 数 |
| `09_duplex_widths` | 36-38 | 8/16/32 bit，各 8 word | Duplex | 三种位宽下 MOSI 和 MISO 的独立图样 | 主机、从机两侧均为 `match` |
| `10_speed_100000Hz` | 39 | 8 bit，8 word，100 kHz 请求 | RX | 低速基线 | 从机 `match` |
| `10_speed_1000000Hz` | 40 | 8 bit，8 word，1 MHz 请求 | RX | 常用工作点 | 从机 `match` |
| `10_speed_10000000Hz` | 41 | 8 bit，8 word，10 MHz 请求 | RX | 中速工作点 | 从机 `match` |
| `10_speed_25000000Hz` | 42 | 8 bit，8 word，25 MHz 请求 | RX | 高速工作点 | 从机 `match` |
| `10_speed_40000000Hz` | 43 | 8 bit，8 word，40 MHz 请求 | RX | 请求上限点 | 从机 `match` |

基础、重复 arm、长接收、从机 TX、全双工八组使用主机 `--speed` 参数；速度扫描始终使用表中的 5 个固定请求值。

### 4.3 各方向真正校验的信号

| `--direction` | 主机动作 | 从机动作 | 有效比较结果 |
|---|---|---|---|
| `rx` | 在 MOSI 发送主机图样；MISO 不比较 | 从 DR 接收并比较主机图样 | 从机的 `result=match` |
| `tx` | 发送时钟和主机图样，同时从 MISO 接收并比较从机图样 | 预装从机图样到 TX FIFO；接收仅用于完成时钟驱动 | 主机的 `miso=match`；从机显示 `result=clocked` |
| `duplex` | MOSI 发送主机图样，同时比较 MISO 从机图样 | 预装从机图样，同时接收并比较 MOSI 主机图样 | 主机、从机两侧均为 `match` |

## 5. 本项目能够检测的问题

| 问题类别 | 主要触发用例 | 典型现象 | 可定位范围 |
|---|---|---|---|
| mode 配置错误或回读错误 | `01_config`、全部数据组 | mode 非 3 被驱动拒绝、配置字段不匹配或数据整体异常 | `CMD_set_mode` / `CMD_get_info`、SSLV mode 限制、主从 CPOL/CPHA 一致性 |
| 非法 bpw / busy 状态未保护 | `01_config` | 非法参数被意外接受，4/32 bit 回读错误，或 enable 后仍可改 bpw | `CMD_set_bits` / `CMD_get_info` 参数检查、状态机保护 |
| 8/16/32-bit 访问宽度错误 | `02_basic_rx` | 单一位宽失败，或 `word-byte-swap` | `to_bytes()`、用户缓冲区长度、word 打包方式 |
| MSB/LSB bit 顺序错误 | 任意 RX / TX / duplex 组 | `bit-reverse` 或组合结果 | SPI bit order、控制器配置 |
| 多字节字节序错误 | 16/32-bit 组 | `word-byte-swap` | `u16/u32` 读写和协议组包 |
| CS 边界或重 arm 时序错误 | `03_rearm` | sequence 不连续、上一帧图样混入下一帧 | disable/enable 顺序、CS 间隔、FIFO 残留 |
| RX FIFO 阈值 / 中断取数错误 | `04_long_rx` | 短接收、后半帧错误或 receive 超时 | RXFTLR、IRQ、`rlen/rbuff` 推进 |
| 从机 DT 或主机 MISO 单向故障 | `05_slave_tx_widths`、`06_slave_tx_min` | 主机 `miso=mismatch`，从机仍可能 `clocked` | DT/MISO 接线、TX FIFO 预装载、主机采样 |
| TX FIFO 满载或尾随零路径错误 | `07_slave_tx_fifo_limit`、`08_slave_tx_append_zero` | 64-word 回传短帧、尾随零错误或 `send` 返回长度错误 | `CMD_send`、TX FIFO 容量、零 word 续发和 ABI 返回值 |
| 一侧收发同时工作异常 | `09_duplex_widths` | 仅 MOSI 或仅 MISO 失败 | 全双工配置、两个方向的 FIFO / 引脚 |
| 高速边界或信号质量问题 | `10_speed_*` | 低速通过、高速 `mismatch` 或 `receive=-1` | 实际 SCLK、CS 建立保持、走线、地线、电平、采样裕量 |

## 6. 不通过时如何理解输出

### 6.1 配置组失败

```text
[suite] 01_config FAIL: unexpected configuration result
```

表示至少一个非法 mode/bpw 或 busy 状态下的重新配置没有被预期地拒绝，或者从机无法重新回到 mode 3、bpw 8 的可用状态。此失败不涉及外部 SPI 线，应先检查 SSLV ioctl 和状态机。

### 6.2 收不到数据或接收超时

```text
[slave] case=43 bits=8 receive=-1 expected=8
[suite] 10_speed_40000000Hz FAIL: case=43 receive=-1 expected=8
```

表示从机没有在该 case 得到完整预期长度。常见原因包括主机未在 suite 15 秒窗口内启动、CS/CLK 未到从机、RX threshold IRQ 未到、控制器仍忙或高速下接收失败。排查顺序：先核对主从 case 顺序和共地，再比较 `SSI_SLV` 中断计数，最后在故障现场读取 RXFLR、IMR、ISR、RISR 并采集波形。

### 6.3 数据错位或残留 FIFO

```text
[slave] expected: 6b 8d dc e3 ...
[slave] actual:   34 12 5a a5 7e 81 fe 01 ...
```

如果 `actual` 的开头是上一 case 的图样，说明主从已失步或 FIFO 中有旧数据。前一个失败 case 才是优先根因；不要先把后续 `mismatch` 当作独立的 16-bit 字节序问题。

### 6.4 从机回传或全双工失败

```text
[master] case=20 direction=tx ... miso=mismatch
[master] expected miso: ...
[master] actual miso:   ...
```

`tx` 模式只以主机 MISO 比较为准，优先检查从机 DT、主机 MISO、从机 TX FIFO 预装载和主机采样。`duplex` 模式下，如果主机 MISO 通过、从机 MOSI 失败，问题在主机到从机方向；反之则在从机到主机方向。

### 6.5 尾随零 word 或 send 返回值失败

```text
[slave] case=33 slave TX returned 1 expected=8
[suite] 08_slave_tx_append_zero FAIL: case=33 slave TX returned 1 expected=8
```

`08_slave_tx_append_zero` 同时检查数据和接口返回值：主机必须收到 8 个从机图样 word 后紧跟 1 个全零 word；从机 `sslv_send(..., add_zero=1)` 必须返回请求的 payload 字节数。若主机 MISO 为 `match` 但从机报告返回值错误，说明数据路径可能正常而 `CMD_send` 的返回值仍不符合用户态接口契约。

### 6.6 仅高速点失败

速度扫描中的标签是**请求频率**，不是波形测得的实际频率。当前 X2600 主机驱动按 `div_ssi` 和整数 CGV 分频选择邻近值；例如 `div_ssi=100 MHz` 时，请求 40 MHz 会选择 50 MHz，而不是精确 40 MHz。因此必须在 NAND 主机侧检查：

```sh
cat /sys/module/soc_spi/parameters/div_ssi_rate
dmesg | grep -E 'SPI1 freq|spi set speed invalid'
```

并用逻辑分析仪确认 SCLK 周期、占空比和 CS 窗口。若低频稳定、高频失败，不能直接归因于从机软件“读 FIFO 太慢”：8-byte 高速 case 只需一次阈值中断，更常见的是实际时钟超过预期、信号完整性、CS 时序或采样裕量不足。

## 7. 构建和手工测试

### 7.1 构建

```sh
cd "2.从机模式测试"
make test
make
```

`make test` 在开发机运行图样比较、主机 suite ioctl mock、从机配置回读 stub 和从机完整 suite stub；`make` 使用 Makefile 中的 MIPS 交叉编译器生成板端程序。目标程序不能在 x86 开发机直接执行。

### 7.2 单用例排查

先验证最低复杂度的 8-bit 单帧：

```sh
# NOR 从机，先运行
./spi_endian_slave --device /dev/sslv0 \
    --bits 8 --words 8 --loops 1 --direction rx

# NAND 主机，后运行
./spi_endian_master --device /dev/spidev1.0 \
    --speed 1000000 --bits 8 --words 8 --loops 1 --direction rx
```

再单独验证长接收和全双工：

```sh
# 长接收：主从参数必须一致
./spi_endian_slave --bits 16 --words 128 --loops 10 --direction rx
./spi_endian_master --device /dev/spidev1.0 \
    --speed 1000000 --bits 16 --words 128 --loops 10 --direction rx

# 全双工：主从参数必须一致
./spi_endian_slave --bits 16 --words 8 --loops 1 --direction duplex
./spi_endian_master --device /dev/spidev1.0 \
    --speed 1000000 --bits 16 --words 8 --loops 1 --direction duplex
```

测试前可清除内核日志，测试后读取 SSLV 信息：

```sh
dmesg -c >/dev/null
# 执行一轮主从测试
dmesg | grep SSLV
grep SSI_SLV /proc/interrupts
```

## 8. 本项目不覆盖的内容

通过本项目不等于 SSLV 的所有功能已经完成资格验证。当前未覆盖或不能由此证明的内容包括：

- mode 0、1、2 和 LSB-first；当前 SSLV 用户接口只支持 mode 3。
- SSP、Microwire、硬件 loopback、SSLV DMA 和 callback 接收；当前 app 没有对应的用户接口路径。
- RX overflow、TX overflow、underflow 的可靠错误返回；suite 不主动制造溢出，且不能以 `match` 证明异常路径正确。
- 多进程并发访问、进程被信号杀死、`close()` 后硬件自动清理和热插拔恢复。
- 持续数小时/数天的老化、温度、电压波动、EMI 和电源完整性。
- 实际 SCLK 频率、占空比、CS 建立/保持时间、上升/下降沿、过冲和振铃；这些必须由逻辑分析仪或示波器验证。
- 协议层 CRC、帧头、长度字段、重传和流控；本程序只验证原始 SPI 数据字节流。

最新的 44-case 双板结果见 [test_result/X2600-SPI主从综合测试报告-2026-08-10-44case.md](test_result/X2600-SPI主从综合测试报告-2026-08-10-44case.md)，扩展前的 29-case 归档见 [test_result/X2600-SPI主从综合测试报告-2026-08-10.md](test_result/X2600-SPI主从综合测试报告-2026-08-10.md)。
