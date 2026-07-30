# X2600 SPI 60 MHz Qualification Design

## Goal

Refactor `app_spi` into a focused 60 MHz SPI test tool. Remove obsolete
continuous-clock support and legacy test entry points. Retain only:

- `--mode delay` for software-GPIO and hardware-CE0 timing capture.
- `--mode complete` for a quick external-loopback maximum-frequency check.
- `--qualification` for the automatic SPI0-then-SPI1 long qualification run.

The qualification command runs a short no-error preflight before the long
test for each bus. A preflight failure skips that bus's long test, records the
failure, and continues with the other bus unless the operator interrupts.

## Fixed Hardware Profile

Qualification always runs these two profiles in order. It does not accept a
bus or CS override, so the test report is reproducible.

| Bus | SCLK | MOSI | MISO | Software CS |
|---|---|---|---|---|
| SPI0 | PD00 | PD01 | PD02 | PC09 |
| SPI1 | PC25 | PC26 | PC27 | PC30 |

The operator must short MOSI to MISO at the SoC side for the selected bus and
pass `--ssi-source-hz 120000000`. The command requires the calculated maximum
SCLK and the highest error-free external-loopback speed to both be 60 MHz.

## Command Surface

`--mode` accepts only `complete` (default) and `delay`.

`--qualification` is a dedicated trigger and cannot be combined with
`--mode delay`, `--bus`, `--cs`, or `--hw-cs`. It automatically creates and
tests the SPI0 profile, then the SPI1 profile.

The removed interfaces are `internal`, `external`, `instrument`, `freq`,
`--freq`, all `continuous_*` module-parameter access, and stale unused stress
or legacy-case options.

## Test Behavior

### Delay

`--mode delay` retains the existing software-CS and hardware-CE0 capture
workflow. Its requested SCLK sweep is 1 MHz, 10 MHz, and 60 MHz. Instrument
measurement is still required for actual SCLK and CS-to-first-clock timing.

### Complete

`--mode complete` performs a quick external full-duplex loopback scan. It
tests the configured scan frequencies, including 60 MHz, with four data
patterns and reports the highest error-free request frequency and effective
payload bandwidth. It uses only an external full-duplex RX comparison; there
are no standalone write-only or read-only matrix cases.

### Qualification

For each bus, qualification executes the following sequence:

1. Run the same external full-duplex maximum-frequency scan as `complete`.
2. Require a 60 MHz maximum result.
3. Preflight all nine lengths: 16 KiB, 128 KiB, 1 MiB, 127 B, 128 B, 129 B,
   4095 B, 4096 B, and 4097 B. Each length first receives one internal-LOOP
   data-integrity check, then at least five seconds of external full-duplex
   random-data checking.
4. Collect external full-duplex bandwidth samples for the primary lengths
   (16 KiB, 128 KiB, 1 MiB): 1000 samples per length, each lasting at least
   2.5 seconds.
5. Collect external full-duplex bandwidth samples for the six boundaries:
   60 samples per length, each lasting at least five seconds.
6. Run a 64 KiB external full-duplex random-data stress test for 1.5 hours.
   Every transfer is compared with its expected data while total effective
   bandwidth is measured.

Every external full-duplex transfer has both TX and RX buffers and compares
them byte-for-byte. The physical MOSI/MISO short therefore verifies the
controller data path and board routing. Internal LOOP is a controller-only
precheck and is never reported as external pin bandwidth.

## Statistics And Reports

One Markdown report contains environment data, state transitions, summary
statistics, and failures for both buses. A sibling CSV file records every
individual bandwidth sample for auditability.

For each sample set, the Markdown report includes count, total payload bytes,
median, minimum, maximum, arithmetic mean, and sample standard deviation.
The CSV fields are bus, phase, transfer length, sample index, elapsed time,
payload bytes, and effective Mbit/s.

All byte counts, transfer counts, and error counts use 64-bit unsigned types.
Sample timing and statistics use `double`; standard deviation uses the stable
online Welford accumulator. The sample array needed for the 1000-value median
is bounded at 1000 elements. The stress test records 64-bit totals, including
its measured effective bandwidth, rather than extrapolating from SCLK.

## Runtime

Per SPI bus, the long sections take at least 2 hours 5 minutes for primary
samples, 30 minutes for boundaries, and 1.5 hours for stress. The preflight
adds about 45 seconds plus the frequency scan. SPI0 then SPI1 takes a little
more than 8 hours 10 minutes before any operator CS timing work.

## Error Handling

An ioctl error, allocation failure, or byte mismatch fails the active case and
records its first failing byte when available. A preflight failure skips only
the affected bus's long sections. A failure during a long sample set or stress
test stops that active case and proceeds to the next configured bus. SIGINT or
SIGTERM stops cleanly after the current transfer and writes partial results.

## Documentation Scope

Update `README.md` with only the surviving commands, SPI0/SPI1 wiring, 60 MHz
preconditions, qualification runtime, CSV report output, and the requirement
to run SPI0 and SPI1 sequentially through one qualification command. Existing
historical files under `test_results/` are retained unchanged.
