# /home/devvean/work/linux 修改记录

记录时间：2026-07-29 至 2026-07-30

本文件只记录本次由 Codex 在 `/home/devvean/work/linux` 范围内作出的改动。

## 1. `module_driver/soc/x2600_510/spi/spi.c`

修改前备份：

```text
module_driver/soc/x2600_510/spi/spi.c.bak_20260729_continuous_clk
```

新增 SPI 连续时钟输出模式，供测试程序的 `app_spi --freq HZ --bus N` 使用：

- 新增模块参数：
  - `continuous_spi_id`：连续输出使用的 SPI 控制器，默认 `1`。
  - `continuous_clk_rate`：请求的 SCLK 频率，默认 `48000000` Hz。
  - `continuous_enable`：写 `1` 启动，写 `0` 停止。
- 使用 TX/RX 循环 DMA，不经过用户态 spidev 分块传输：
  - TX 4 KiB 循环缓冲区填充 `0x55`，MSB-first、mode 0 时 MOSI 为重复的 `01010101`。
  - RX 循环 DMA 持续回收 RX FIFO，避免 FIFO 溢出。
- 连续模式工作期间，同一 SPI 控制器的普通 SPI message 返回 `-EBUSY`；停止或驱动退出时终止 DMA、关闭 SSI、清空 FIFO 并释放 DMA 缓冲区。
- 修复连续 DMA 的 `dma_slave_config`：TX/RX 两端均设置 1-byte `src_addr_width`、`dst_addr_width` 以及 64-word `src_maxburst`、`dst_maxburst`。
  - 修复依据：板端启动连续模式时 DMA 日志报 `src_addr_width should be greater than 0`；原有正常 SPI DMA 路径已对两端完整设置这些字段。

生效条件：必须重新编译并加载新的 `soc_spi.ko`；仅更新 `app_spi` 不会使此驱动改动生效。

## 2. `module_driver/package/soc/x2600_510/spi/spi.mk`

在 `spi_finalize_hook` 中新增：

```make
$(MAKE) -C $(spi_test_dir) app
cp $(spi_test_dir)/app_spi output/
```

作用：构建 `soc_spi` 包时编译测试目录的 `app_spi`，并放入 `module_driver/output/`。顶层镜像构建完成后，该文件随 module_driver 包安装到 rootfs 的 `/module_driver/app_spi`。

## 未修改范围

- 未修改 `/home/devvean/work/linux/kernel` 下的内核、设备树、Kconfig 或 defconfig 文件。
- 未修改 `/home/devvean/work/linux/bootloader`、`/home/devvean/work/linux/build` 或 rootfs 配置文件。
- `spi.c` 备份中原有的 SPI 频率调试、仅发送轮询路径等改动不是本次连续 DMA 功能新增内容，已保留，未撤销。

## 关联但不在 `/home/devvean/work/linux` 范围内的改动

文件：

```text
/home/devvean/devvean-开发文档/0.工作任务/3.x2600-SPI片上外设测试/app_spi.c
```

- 支持 `--freq HZ --bus N`，通过 `/sys/module/soc_spi/parameters/continuous_*` 控制连续 DMA。
- 默认报告目录改为 `/tmp`，避免 rootfs `/module_driver` 只读导致启动失败。
- `--report FILE` 仍可显式指定报告路径。

## 重新构建与板端使用

重新构建并烧录镜像后，板端运行：

```sh
/module_driver/app_spi --freq 48000000 --bus 1 --report /tmp/spi_freq.md
```

成功后程序持续输出 PC25 SCLK 与 PC26 的 `01010101`；按 `Ctrl-C` 停止。若使用默认报告路径，报告保存到 `/tmp/spi_test_report_*.md`。
