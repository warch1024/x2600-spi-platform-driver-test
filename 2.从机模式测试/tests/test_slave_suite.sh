#!/bin/sh
set -eu

slave=$1
suite_log=$(mktemp /tmp/spi_endian_slave_suite.XXXXXX.md)

$slave --device /dev/sslv0 --suite --log "$suite_log" >/dev/null

grep -q '\*\*08_slave_tx_append_zero\*\*: \*\*PASS\*\*' "$suite_log"
grep -q '\*\*overall\*\*: \*\*PASS\*\*' "$suite_log"
rm -f "$suite_log"
