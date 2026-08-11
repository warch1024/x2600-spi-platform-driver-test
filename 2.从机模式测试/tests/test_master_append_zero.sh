#!/bin/sh
set -eu

master=$1
suite_log=$(mktemp /tmp/spi_endian_append_zero.XXXXXX.md)

$master --device /dev/null --suite --log "$suite_log" --start-delay-ms 0 --case-delay-ms 0 >/dev/null

grep -q '\*\*08_slave_tx_append_zero\*\*: \*\*PASS\*\*' "$suite_log"
grep -q '\*\*overall\*\*: \*\*PASS\*\*' "$suite_log"
rm -f "$suite_log"
