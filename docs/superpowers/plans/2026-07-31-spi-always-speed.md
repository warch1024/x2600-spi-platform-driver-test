# SPI Always-Speed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a TX-only `--mode always-speed HZ` instrument waveform mode.

**Architecture:** Extend the existing mode enum and argument parser with one mode-specific frequency. Reuse the detected spidev buffer size and raw transfer helper, but allocate only TX data and send `0x55` continuously. Progress is emitted outside the transfer path every two seconds.

**Tech Stack:** GNU99 C, Linux spidev ioctl, libhardware2 SPI, POSIX monotonic clock.

---

### Task 1: Add the TX-only always-speed runner

**Files:**
- Modify: `app_spi.c`

- [ ] **Step 1: Extend parsing and help.**

Add `TEST_MODE_ALWAYS_SPEED`, `unsigned int always_speed` in
`test_options_t`, and accept `--mode always-speed HZ`. Reject zero frequency
and requests above `max_supported_sclk(--ssi-source-hz)` when a source clock
was provided. Add the exact command syntax to `print_usage()`.

- [ ] **Step 2: Implement continuous TX.**

Add this runner:

```c
static int run_always_speed(const test_options_t *opt, size_t spidev_bufsiz);
```

It registers the selected software-CS device, configures mode 0/8-bit/MSB
first at `opt->always_speed`, allocates one TX buffer of `spidev_bufsiz`, fills
it with `0x55`, and repeatedly calls `raw_transfer(fd, tx, NULL, len, speed,
0)`. It uses `uint64_t` counters, prints progress at two-second monotonic
intervals, treats SIGINT as success, and reports a final byte/transfer/bandwidth
summary without an error-count field.

- [ ] **Step 3: Wire the mode into `main()`.**

Dispatch `TEST_MODE_ALWAYS_SPEED` before complete mode. Keep delay, complete,
and qualification behavior unchanged.

- [ ] **Step 4: Compile and inspect.**

Run: `make test && make app`

Expected: both exit 0 with no warnings.

Run: `strings app_spi | rg 'always-speed|0x55|01010101'`

Expected: all three strings are present.

### Task 2: Document the instrument workflow

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add an Always-Speed section.**

Document the command:

```sh
./app_spi --mode always-speed 60000000 --ssi-source-hz 120000000
```

State that it sends `0x55` as `01010101` on MOSI, uses one detected spidev
buffer per transfer, needs only SCLK/MOSI/GND instrument connections, prints
two-second non-per-transfer progress, and exits normally on Ctrl-C.

- [ ] **Step 2: Run final verification.**

Run: `make test && make app && git diff --check`

Expected: all commands exit 0.
