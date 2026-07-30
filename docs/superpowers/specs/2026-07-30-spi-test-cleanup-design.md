# SPI Test Cleanup Design

## Scope

Remove all `#if 0` code from `app_spi.c`. These blocks implement a legacy
functional-regression matrix and are not reachable from the current X2600
performance-test modes.

Add focused Chinese comments to the active test paths. Comments will explain
the `libhardware2` SPI device lifecycle, configuration verification, GPIO to
hardware-CE0 pinmux switching, cleanup ordering, and the sysfs module
parameters used by continuous-frequency mode.

## Constraints

The command-line interface, test selection, transfer behavior, and platform
driver interaction must remain unchanged. Verification will compile the source
and confirm no disabled-preprocessor blocks remain.
