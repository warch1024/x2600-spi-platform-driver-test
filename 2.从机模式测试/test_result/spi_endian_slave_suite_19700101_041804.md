# SPI SSLV Slave Suite Report

## Results

[suite] report=./spi_endian_slave_suite_19700101_041804.md
[slave] suite device=/dev/sslv0; wait for master suite
[suite] 01_config        PASS: invalid settings rejected
- **01_config**: **PASS** - invalid settings rejected
[slave] case=0 direction=rx bits=8 words=8 ready; start master now
[slave] case=0 direction=rx bits=8 words=8 result=match
[slave] case=1 direction=rx bits=16 words=8 ready; start master now
[slave] case=1 direction=rx bits=16 words=8 result=match
[slave] case=2 direction=rx bits=32 words=8 ready; start master now
[slave] case=2 direction=rx bits=32 words=8 result=match
[suite] 02_basic_rx      PASS: all cases passed
- **02_basic_rx**: **PASS** - all cases passed
[slave] case=3 direction=rx bits=8 words=8 ready; start master now
[slave] case=3 direction=rx bits=8 words=8 result=match
[slave] case=4 direction=rx bits=16 words=8 ready; start master now
[slave] case=4 direction=rx bits=16 words=8 result=match
[slave] case=5 direction=rx bits=32 words=8 ready; start master now
[slave] case=5 direction=rx bits=32 words=8 result=match
[slave] case=6 direction=rx bits=8 words=8 ready; start master now
[slave] case=6 direction=rx bits=8 words=8 result=match
[slave] case=7 direction=rx bits=16 words=8 ready; start master now
[slave] case=7 direction=rx bits=16 words=8 result=match
[slave] case=8 direction=rx bits=32 words=8 ready; start master now
[slave] case=8 direction=rx bits=32 words=8 result=match
[slave] case=9 direction=rx bits=8 words=8 ready; start master now
[slave] case=9 direction=rx bits=8 words=8 result=match
[slave] case=10 direction=rx bits=16 words=8 ready; start master now
[slave] case=10 direction=rx bits=16 words=8 result=match
[slave] case=11 direction=rx bits=32 words=8 ready; start master now
[slave] case=11 direction=rx bits=32 words=8 result=match
[slave] case=12 direction=rx bits=8 words=8 ready; start master now
[slave] case=12 direction=rx bits=8 words=8 result=match
[slave] case=13 direction=rx bits=16 words=8 ready; start master now
[slave] case=13 direction=rx bits=16 words=8 result=match
[slave] case=14 direction=rx bits=32 words=8 ready; start master now
[slave] case=14 direction=rx bits=32 words=8 result=match
[slave] case=15 direction=rx bits=8 words=8 ready; start master now
[slave] case=15 direction=rx bits=8 words=8 result=match
[slave] case=16 direction=rx bits=16 words=8 ready; start master now
[slave] case=16 direction=rx bits=16 words=8 result=match
[slave] case=17 direction=rx bits=32 words=8 ready; start master now
[slave] case=17 direction=rx bits=32 words=8 result=match
[suite] 03_rearm         PASS: all cases passed
- **03_rearm**: **PASS** - all cases passed
[slave] case=18 direction=rx bits=16 words=128 ready; start master now
[slave] case=18 direction=rx bits=16 words=128 result=match
[slave] case=19 direction=rx bits=16 words=128 ready; start master now
[slave] case=19 direction=rx bits=16 words=128 result=match
[suite] 04_long_rx       PASS: all cases passed
- **04_long_rx**: **PASS** - all cases passed
[slave] case=20 direction=tx bits=16 words=8 ready; start master now
[slave] case=20 direction=tx bits=16 words=8 result=clocked
[slave] case=21 direction=tx bits=16 words=8 ready; start master now
[slave] case=21 direction=tx bits=16 words=8 result=clocked
[suite] 05_slave_tx      PASS: all cases passed
- **05_slave_tx**: **PASS** - all cases passed
[slave] case=22 direction=duplex bits=16 words=8 ready; start master now
[slave] case=22 direction=duplex bits=16 words=8 result=match
[slave] case=23 direction=duplex bits=16 words=8 ready; start master now
[slave] case=23 direction=duplex bits=16 words=8 result=match
[suite] 06_duplex        PASS: all cases passed
- **06_duplex**: **PASS** - all cases passed
[slave] case=24 direction=rx bits=8 words=8 ready; start master now
[slave] case=24 direction=rx bits=8 words=8 result=match
[suite] 07_speed_100000Hz PASS: all cases passed
- **07_speed_100000Hz**: **PASS** - all cases passed
[slave] case=25 direction=rx bits=8 words=8 ready; start master now
[slave] case=25 direction=rx bits=8 words=8 result=match
[suite] 07_speed_1000000Hz PASS: all cases passed
- **07_speed_1000000Hz**: **PASS** - all cases passed
[slave] case=26 direction=rx bits=8 words=8 ready; start master now
[slave] case=26 direction=rx bits=8 words=8 result=match
[suite] 07_speed_10000000Hz PASS: all cases passed
- **07_speed_10000000Hz**: **PASS** - all cases passed
[slave] case=27 direction=rx bits=8 words=8 ready; start master now
[slave] case=27 direction=rx bits=8 words=8 result=match
[suite] 07_speed_25000000Hz PASS: all cases passed
- **07_speed_25000000Hz**: **PASS** - all cases passed
[slave] case=28 direction=rx bits=8 words=8 ready; start master now
[slave] case=28 bits=8 receive=-1 expected=8
[suite] 07_speed_40000000Hz FAIL: case=28 receive=-1 expected=8
- **07_speed_40000000Hz**: **FAIL** - case=28 receive=-1 expected=8
[suite] overall          FAIL: one or more groups failed
- **overall**: **FAIL** - one or more groups failed

Result: FAIL (pass=10 fail=2)
