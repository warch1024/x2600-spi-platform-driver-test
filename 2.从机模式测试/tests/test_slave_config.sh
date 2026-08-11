#!/bin/sh
set -eu

slave=$1
output=$($slave --device /dev/sslv0 --config-check 2>&1)
usage=$($slave --bits 7 2>&1 || true)

printf '%s\n' "$output" | grep -q 'configuration checks passed'
printf '%s\n' "$usage" | grep -q -- '--suite'
