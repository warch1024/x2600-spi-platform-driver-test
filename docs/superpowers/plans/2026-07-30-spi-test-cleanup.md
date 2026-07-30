# SPI Test Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove unreachable SPI regression code and document every active platform-driver interaction in the X2600 SPI test.

**Architecture:** Keep `app_spi.c` as the single test binary. Delete four disabled preprocessor regions without replacing their APIs, then document the active user-space-to-driver lifecycle at device registration, SPI configuration, pinmux/CE0 switching, sysfs parameter writes, and cleanup.

**Tech Stack:** GNU99 C, Linux spidev ioctl, libhardware2 SPI/GPIO APIs, soc_spi module sysfs parameters.

---

### Task 1: Remove disabled legacy regression code

**Files:**
- Modify: `app_spi.c:891-925,939-961,1088-1242,1517-1948`
- Test: `app_spi.c`

- [ ] **Step 1: Remove every `#if 0` region and the declarations it encloses.**

  Delete the legacy transfer helper, burst helper, GPIO/BPW/pattern regression matrix, and legacy transfer suites. Retain active helpers such as `aligned_len` only if they remain referenced after removal.

- [ ] **Step 2: Verify no disabled preprocessor code remains.**

  Run: `rg -n '^#if 0|^#if defined\(0\)' app_spi.c`

  Expected: no output and exit status 1.

### Task 2: Document active platform-driver boundaries

**Files:**
- Modify: `app_spi.c:484-561,678-709,831-883,1411-1498,2000-2176`
- Test: `app_spi.c`

- [ ] **Step 1: Add Chinese comments for `libhardware2` device setup and teardown.**

  Explain that `spi_add_device()` registers a temporary platform-backed SPI child and returns the expected spidev path; wait for probe/devtmpfs before opening it; close the user descriptor before deleting the device.

- [ ] **Step 2: Add Chinese comments for configuration and transfers.**

  Explain that `spi_set_*()` forwards settings through the driver, `spi_get_info()` checks the effective spidev state, and `raw_transfer()` builds the single-message ioctl that the driver receives.

- [ ] **Step 3: Add Chinese comments for continuous DMA sysfs controls.**

  Explain that the parameters belong to `soc_spi`, are configured while output is disabled, then enabled atomically from the test's perspective, and are disabled during cleanup after Ctrl-C.

- [ ] **Step 4: Add Chinese comments for CE0 pinmux and test-mode transitions.**

  Explain that the software GPIO device must be released before the pin is muxed to SSI CE0; `--cs -1` makes the platform driver own hardware CS; failures and final cleanup restore input/high-impedance state.

### Task 3: Compile and inspect the resulting source

**Files:**
- Test: `app_spi.c`

- [ ] **Step 1: Compile with the project's configured cross compiler.**

  Run: `make app`

  Expected: exit status 0, producing `app_spi` with no compiler diagnostics.

- [ ] **Step 2: Re-run the disabled-code scan.**

  Run: `! rg -n '^#if 0|^#if defined\(0\)' app_spi.c`

  Expected: exit status 0.

- [ ] **Step 3: Inspect the source diff.**

  Run: `git diff --no-index /dev/null app_spi.c` is not applicable because this workspace has no Git metadata. Use `rg -n 'spi_add_device|spi_set_mode|gpio_set_func|continuous_enable|spi_context_deinit' app_spi.c` to review the documented active boundaries.
