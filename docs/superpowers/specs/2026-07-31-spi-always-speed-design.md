# SPI Always-Speed Design

## Goal

Add an instrument-oriented continuous send mode:

```sh
./app_spi --mode always-speed 60000000 --ssi-source-hz 120000000
```

It produces a repeated `0x55` TX pattern for SCLK/MOSI observation. In SPI
mode 0, 8-bit, MSB-first, this is a repeated `01010101` MOSI waveform.

## Behavior

- `--mode always-speed HZ` is the only mode value with a required frequency
  argument.
- It uses the detected `spidev.bufsiz` as each single spidev TX message.
- It configures mode 0, 8-bit, MSB-first and the requested frequency.
- It passes no RX buffer and performs no loopback comparison; MOSI/MISO do
  not need to be shorted.
- It rejects a requested frequency above the maximum calculated from a supplied
  SSI source clock.
- The transfer loop has no per-transfer output. Every two seconds it prints
  cumulative logical TX bytes, transfer count, and effective payload Mbit/s.
- Ctrl-C is a normal termination. The final result reports elapsed time,
  cumulative bytes, transfer count, and effective bandwidth, without an error
  count.
- An ioctl/configuration/allocation failure terminates the mode as failed and
  reports the failure reason.

## Scope

Update `app_spi.c` command parsing and add one TX-only loop runner. Update
`README.md` with the command, instrument connections, and the distinction from
data-integrity tests. Existing qualification and complete behavior are
unchanged.
