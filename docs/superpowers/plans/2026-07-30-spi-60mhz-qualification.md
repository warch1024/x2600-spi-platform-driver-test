# SPI 60 MHz Qualification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the legacy SPI test surface with delay, quick complete, and an automatic SPI0-then-SPI1 60 MHz qualification run that records verified bandwidth statistics and stress bandwidth.

**Architecture:** Keep platform SPI/GPIO lifecycle, reports, transfer construction, and all three test runners in a rewritten `app_spi.c`. Move only bounded sample statistics into `spi_stats.c` so median and sample standard deviation have host-runnable unit tests. Qualification uses fixed SPI0/SPI1 profiles, a per-bus preflight gate, then primary samples, boundary samples, and stress; its Markdown report links to an audit CSV of individual samples.

**Tech Stack:** GNU99 C, Linux spidev ioctl, `libhardware2` SPI/GPIO API, POSIX monotonic clock, C standard library math, host `cc` tests, MIPS cross compiler.

---

## File Structure

- Create: `spi_stats.h` - bounded sample container and statistics API.
- Create: `spi_stats.c` - Welford accumulation, sorting-based median, and sample standard deviation.
- Create: `tests/test_spi_stats.c` - host unit tests for all statistics boundaries.
- Modify: `Makefile` - link `spi_stats.c`, `-lm`, and add a host `test` target.
- Rewrite: `app_spi.c` - only delay, complete, and qualification flows; no continuous-clock, legacy modes, or unused options.
- Rewrite: `README.md` - only the surviving 60 MHz commands and qualification procedure.
- Create: `docs/superpowers/specs/2026-07-30-spi-60mhz-qualification-design.md` already exists and remains the approved behavior reference.

Historical reports under `test_results/` and `work_linux_modify_record/README.md` are not modified. This directory is not a Git repository, so do not create commits; use build/test evidence and `diff -u` backups for review instead.

### Task 1: Add host-testable sample statistics

**Files:**
- Create: `spi_stats.h`
- Create: `spi_stats.c`
- Create: `tests/test_spi_stats.c`

- [ ] **Step 1: Write the failing statistics test.**

Create `tests/test_spi_stats.c` with an assertion helper and these exact checks:

```c
#include "../spi_stats.h"
#include <assert.h>
#include <math.h>

static void close_to(double actual, double expected) {
  assert(fabs(actual - expected) < 0.000001);
}

int main(void) {
  spi_stats_t stats;

  spi_stats_init(&stats);
  spi_stats_add(&stats, 1.0);
  spi_stats_add(&stats, 2.0);
  spi_stats_add(&stats, 3.0);
  spi_stats_add(&stats, 4.0);
  assert(stats.count == 4);
  close_to(spi_stats_min(&stats), 1.0);
  close_to(spi_stats_max(&stats), 4.0);
  close_to(spi_stats_mean(&stats), 2.5);
  close_to(spi_stats_median(&stats), 2.5);
  close_to(spi_stats_sample_stddev(&stats), 1.2909944487358056);

  spi_stats_init(&stats);
  spi_stats_add(&stats, 9.0);
  spi_stats_add(&stats, 1.0);
  spi_stats_add(&stats, 5.0);
  close_to(spi_stats_median(&stats), 5.0);
  close_to(spi_stats_sample_stddev(&stats), 4.0);
  return 0;
}
```

- [ ] **Step 2: Run the host test to verify it fails.**

Run: `cc -std=gnu99 -Wall -Wextra -I. tests/test_spi_stats.c spi_stats.c -lm -o /tmp/test_spi_stats`

Expected: failure because `spi_stats.h` and `spi_stats.c` do not yet exist.

- [ ] **Step 3: Implement the bounded statistics API.**

Create `spi_stats.h` with this public interface:

```c
#ifndef SPI_STATS_H
#define SPI_STATS_H

#include <stddef.h>

#define SPI_STATS_MAX_SAMPLES 1000U

typedef struct {
  double values[SPI_STATS_MAX_SAMPLES];
  size_t count;
  double min;
  double max;
  double mean;
  double m2;
} spi_stats_t;

void spi_stats_init(spi_stats_t *stats);
int spi_stats_add(spi_stats_t *stats, double value);
double spi_stats_min(const spi_stats_t *stats);
double spi_stats_max(const spi_stats_t *stats);
double spi_stats_mean(const spi_stats_t *stats);
double spi_stats_median(const spi_stats_t *stats);
double spi_stats_sample_stddev(const spi_stats_t *stats);

#endif
```

Implement `spi_stats_add()` using Welford's update: increment `count`, update
`mean` with `delta / count`, update `m2` with `delta * (value - mean)`, and
return `-1` if `count` already equals `SPI_STATS_MAX_SAMPLES`. Copy values to
a local array before `qsort()` in `spi_stats_median()` so reporting does not
change sample order. Return `0.0` for an empty statistic and for the standard
deviation of fewer than two values; otherwise return `sqrt(m2/(count-1))`.

- [ ] **Step 4: Run the host test to verify it passes.**

Run: `cc -std=gnu99 -Wall -Wextra -I. tests/test_spi_stats.c spi_stats.c -lm -o /tmp/test_spi_stats && /tmp/test_spi_stats`

Expected: exit status 0 with no output.

### Task 2: Make the project build the statistics module

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Update the application dependency and link list.**

Replace the app target with:

```make
HOST_CC ?= cc
APP_SOURCES := app_spi.c spi_stats.c

app: $(APP_SOURCES) spi_stats.h

	$(APP_CC) $(APP_CFLAGS) -I$(APP_INC) $(APP_SOURCES) -L$(APP_LIB) -L$(UTILS_LIB) -lhardware2 -lutils2 -lm -Wl,-rpath-link,$(UTILS_LIB) -o app_spi

test: spi_stats.c spi_stats.h tests/test_spi_stats.c

	$(HOST_CC) -std=gnu99 -Wall -Wextra -I. tests/test_spi_stats.c spi_stats.c -lm -o /tmp/test_spi_stats
	/tmp/test_spi_stats

.PHONY: app test
```

Preserve the existing `APP_CC`, include, library, and CFLAGS assignments.

- [ ] **Step 2: Run the host test target.**

Run: `make test`

Expected: exit status 0 and no assertion output.

### Task 3: Rewrite the command surface and shared SPI transfer primitives

**Files:**
- Rewrite: `app_spi.c`

- [ ] **Step 1: Remove obsolete code rather than retaining compatibility paths.**

Delete the frequency-output mode, `continuous_*` sysfs helpers, `--freq`,
`internal`, `external`, and `instrument` modes, write-only/read-only payload
cases, legacy disabled code, and unused stress/case options. Do not leave
dead declarations, compatibility aliases, or README-facing hidden switches.

- [ ] **Step 2: Define the surviving configuration types and parser.**

Keep `TEST_MODE_COMPLETE` and `TEST_MODE_DELAY`, add a `qualification` flag,
and accept only these options: `--mode complete|delay`, `--qualification`,
`--bus`, `--cs`, `--hw-cs`, `--no-hw-cs`, `--ssi-source-hz`,
`--max-transfer`, `--loops`, `--cs-arm-ms`, `--cs-to-clk-ns`, and `--report`.
Reject `--qualification` when it is paired with `--mode delay`, `--bus`,
`--cs`, `--hw-cs`, or `--no-hw-cs`. Qualification obtains its fixed profiles
from this table in C:

```c
static const qualification_profile_t qualification_profiles[] = {
    {0U, "pc09", "PD00", "PD01", "PD02"},
    {1U, "pc30", "PC25", "PC26", "PC27"},
};
```

Require `--ssi-source-hz 120000000` for qualification. Its absence or any
other value is a usage error, because the qualification criterion is exactly
60 MHz.

- [ ] **Step 3: Keep one full-duplex transfer path and one internal precheck path.**

Retain `raw_transfer()`, chunking at detected `spidev` buffer size, random
pattern generation, and first-mismatch comparison. Expose a single helper
with this contract:

```c
static int run_checked_transfer(spi_context_t *ctx, int internal_loop,
                                size_t len, uint32_t seed,
                                size_t *bad_offset);
```

It allocates TX/RX buffers, configures mode 0, 8-bit, MSB-first, 60 MHz,
runs a full-duplex transaction, compares TX/RX, and returns `0`, `-EBADE`, or
the underlying transfer error. When `internal_loop` is nonzero it adds
`SPI_LOOP`; otherwise it is external full duplex. `transfer_buffers()` keeps
its 4 KiB-or-detected-limit chunking and records every logical length with
64-bit counters.

- [ ] **Step 4: Build to verify the rewritten source has no stale driver API.**

Run: `make app && ! rg -n 'continuous_|TEST_MODE_FREQUENCY|--freq|run_frequency_output|PAYLOAD_WRITE|PAYLOAD_READ' app_spi.c`

Expected: cross compilation succeeds and the search returns no matches.

### Task 4: Implement 60 MHz delay and quick complete runners

**Files:**
- Modify: `app_spi.c`

- [ ] **Step 1: Update CS timing capture to use 60 MHz.**

Keep the established GPIO-to-CE0 pinmux ownership ordering. Change the delay
speed array to exactly `1000000U, 10000000U, 60000000U`; update every report
heading and message that names 48 MHz to name 60 MHz.

- [ ] **Step 2: Implement the quick complete runner.**

Use an external full-duplex frequency scan on the user-selected bus and CS.
The scan list is `1M, 5M, 10M, 20M, 25M, 50M, 60M`; run four deterministic
patterns at `--max-transfer` for each speed. Report the calculated 60 MHz
ceiling when `--ssi-source-hz 120000000` is supplied, then report the highest
error-free requested speed and its measured effective payload bandwidth.
Do not execute CS timing, a payload matrix, internal-loop bandwidth, or
pressure testing from complete mode.

- [ ] **Step 3: Verify the command surface and build.**

Run: `make app && strings app_spi | rg 'qualification|60MHz|continuous_enable|--freq'`

Expected: the built program contains `qualification` and `60MHz`, and has no
`continuous_enable` or `--freq` string.

### Task 5: Implement qualification preflight, samples, CSV, and stress

**Files:**
- Modify: `app_spi.c`

- [ ] **Step 1: Add report and CSV lifecycle with 64-bit fields.**

Extend the report context with `FILE *samples_fp` and a sibling path formed
by appending `.csv` to the Markdown report path. Open it in `report_open()`,
write this header, and close it in `report_close()`:

```text
bus,phase,length_bytes,sample,elapsed_ms,payload_bytes,mbit_per_s
```

Use `uint64_t` for all completed-byte, transfer-count, and error-count
accumulators. Print such counters with `PRIu64` from `<inttypes.h>`.

- [ ] **Step 2: Implement one timed, verified sample window.**

Add this helper:

```c
static int run_duplex_window(spi_context_t *ctx, size_t len,
                             double minimum_seconds, uint32_t *seed,
                             uint64_t *bytes, double *elapsed_seconds,
                             size_t *bad_offset);
```

It repeatedly calls `run_checked_transfer(ctx, 0, len, (*seed)++, bad_offset)`
until `CLOCK_MONOTONIC` elapsed time reaches `minimum_seconds`. It increments
the `uint64_t` byte counter only after each successful full logical payload,
stops at the first error or interrupt, and returns the measured elapsed time.

- [ ] **Step 3: Implement per-case sample collection and summary.**

Add `run_sample_set()` that receives bus, phase label, length, count, and
minimum duration. It calls `run_duplex_window()` once per sample, writes each
successful result to CSV, adds Mbit/s to `spi_stats_t`, and writes one
Markdown summary row containing count, `PRIu64` payload bytes, median, min,
max, mean, and sample standard deviation. A failed sample terminates that
case and records its first bad offset; partial sample sets are marked failed.

Use these exact arrays:

```c
static const size_t primary_lengths[] = {16U * 1024U, 128U * 1024U,
                                         1024U * 1024U};
static const size_t boundary_lengths[] = {127U, 128U, 129U,
                                          4095U, 4096U, 4097U};
```

Call primary sets with `1000U, 2.5` and boundary sets with `60U, 5.0`.

- [ ] **Step 4: Implement the preflight and stress runners.**

For every primary and boundary length, run one internal-loop checked transfer
followed by `run_duplex_window(..., 5.0, ...)`; a failure marks the bus as
not qualified. Implement stress with 64 KiB windows until monotonic elapsed
time reaches `5400.0` seconds. Keep random seeds advancing, verify every
transfer, and report total 64-bit bytes, transfers, errors, elapsed time, and
effective Mbit/s.

- [ ] **Step 5: Implement automatic SPI0 then SPI1 orchestration.**

`run_qualification()` iterates `qualification_profiles` in declared order,
creates/releases one software-CS `spi_context_t` per profile, runs the
frequency scan, requires 60 MHz, runs preflight, then long samples and stress.
On one bus's failure, finalize that bus section and continue to the next bus.
On `stop_requested`, stop before opening the next profile. Return failure when
either bus fails or has incomplete sample/stress data.

- [ ] **Step 6: Rebuild and inspect safety-critical identifiers.**

Run: `make test && make app && rg -n 'uint64_t|PRIu64|run_duplex_window|run_sample_set|run_qualification|5400\.0|1000U, 2\.5|60U, 5\.0' app_spi.c`

Expected: both builds succeed and all listed qualification safeguards appear.

### Task 6: Replace README with the surviving workflow

**Files:**
- Rewrite: `README.md`

- [ ] **Step 1: Document hardware prerequisites and removed behavior.**

Document the 120 MHz `div_ssi` prerequisite, calculated 60 MHz SCLK, SPI0
and SPI1 pin/CS table, MOSI-to-MISO shorting, common ground, and the need to
use a suitable instrument to confirm 60 MHz. State that continuous clock
output and read-only/write-only performance modes no longer exist.

- [ ] **Step 2: Document exact commands.**

Include these commands with their intended use:

```sh
# Quick, one-bus maximum-frequency and full-duplex check.
./app_spi --mode complete --bus 0 --cs pc09 --ssi-source-hz 120000000 \
  --report /tmp/spi0_complete.md

# Software and hardware CE0 timing capture on SPI1.
./app_spi --mode delay --bus 1 --cs pc30 --ssi-source-hz 120000000
./app_spi --mode delay --bus 1 --hw-cs pc30 --ssi-source-hz 120000000

# Automatic SPI0 followed by SPI1 formal qualification.
./app_spi --qualification --ssi-source-hz 120000000 \
  --report /tmp/spi_qualification.md
```

Explain that qualification creates `/tmp/spi_qualification.md.csv`, runs a
short all-length preflight before each bus's long test, takes a little more
than 8 hours 10 minutes, and stops a failing bus's long work while still
starting the next bus.

- [ ] **Step 3: Verify documentation has no obsolete usage.**

Run: `! rg -n '48MHz|48 MHz|--freq|continuous_|internal、external|仅写|仅读' README.md`

Expected: exit status 0.

### Task 7: Final verification and review

**Files:**
- Verify: `app_spi.c`, `spi_stats.c`, `spi_stats.h`, `tests/test_spi_stats.c`, `Makefile`, `README.md`

- [ ] **Step 1: Run all executable checks.**

Run: `make test && make app`

Expected: both commands exit 0 with no compiler warnings.

- [ ] **Step 2: Scan the source and documentation for removed surface area.**

Run: `! rg -n 'continuous_|TEST_MODE_FREQUENCY|--freq|run_frequency_output|PAYLOAD_WRITE|PAYLOAD_READ|48MHz|48000000U' app_spi.c README.md`

Expected: exit status 0.

- [ ] **Step 3: Inspect the working changes without Git.**

Run: `rg -n 'qualification|run_qualification|run_sample_set|run_duplex_window|SPI_STATS_MAX_SAMPLES|5400\.0' app_spi.c spi_stats.h spi_stats.c README.md && make test`

Expected: one or more matches for each qualification component and a passing
host statistics test.
