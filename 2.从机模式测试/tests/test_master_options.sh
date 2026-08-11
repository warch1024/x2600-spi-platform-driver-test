#!/bin/sh
set -eu

master=$1
output=$($master --device /dev/null --bits 8 --words 64 --direction duplex --start-delay-ms 0 --case-delay-ms 0 2>&1 || true)

printf '%s\n' "$output" | grep -q 'device=/dev/null.*bits=8.*words=64.*direction=duplex.*start_delay=0ms.*case_delay=0ms'

suite_log=$(mktemp /tmp/spi_endian_suite.XXXXXX.md)
suite_output=$($master --device /dev/null --suite --log "$suite_log" \
    --start-delay-ms 0 --case-delay-ms 0 2>&1 || true)
grep -q '\*\*overall\*\*: \*\*FAIL\*\*' "$suite_log"
printf '%s\n' "$suite_output" | grep -q '\[suite\] overall'
for group in \
    04_long_rx \
    05_slave_tx_widths \
    06_slave_tx_min \
    07_slave_tx_fifo_limit \
    08_slave_tx_append_zero \
    09_duplex_widths \
    10_speed_100000Hz \
    10_speed_40000000Hz
do
    grep -q "\\*\\*$group\\*\\*" "$suite_log"
done
rm -f "$suite_log"
