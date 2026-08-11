# SPI SSLV Slave Suite Report

## Results

[suite] report=/tmp/slave_suite.md
[slave] suite device=/dev/sslv0; wait for master suite
[suite] 01_config        PASS: invalid settings rejected
- **01_config**: **PASS** - invalid settings rejected
[slave] case=0 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=0 direction=rx bits=8 words=8 result=match
[slave] case=1 direction=rx bits=16 words=8 bytes=16 ready; start master now
[slave] case=1 direction=rx bits=16 words=8 result=match
[slave] case=2 direction=rx bits=32 words=8 bytes=32 ready; start master now
[slave] case=2 direction=rx bits=32 words=8 result=match
[suite] 02_basic_rx      PASS: all cases passed
- **02_basic_rx**: **PASS** - all cases passed
[slave] case=3 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=3 direction=rx bits=8 words=8 result=match
[slave] case=4 direction=rx bits=16 words=8 bytes=16 ready; start master now
[slave] case=4 direction=rx bits=16 words=8 result=match
[slave] case=5 direction=rx bits=32 words=8 bytes=32 ready; start master now
[slave] case=5 direction=rx bits=32 words=8 result=match
[slave] case=6 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=6 direction=rx bits=8 words=8 result=match
[slave] case=7 direction=rx bits=16 words=8 bytes=16 ready; start master now
[slave] case=7 direction=rx bits=16 words=8 result=match
[slave] case=8 direction=rx bits=32 words=8 bytes=32 ready; start master now
[slave] case=8 direction=rx bits=32 words=8 result=match
[slave] case=9 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=9 direction=rx bits=8 words=8 result=match
[slave] case=10 direction=rx bits=16 words=8 bytes=16 ready; start master now
[slave] case=10 direction=rx bits=16 words=8 result=match
[slave] case=11 direction=rx bits=32 words=8 bytes=32 ready; start master now
[slave] case=11 direction=rx bits=32 words=8 result=match
[slave] case=12 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=12 direction=rx bits=8 words=8 result=match
[slave] case=13 direction=rx bits=16 words=8 bytes=16 ready; start master now
[slave] case=13 direction=rx bits=16 words=8 result=match
[slave] case=14 direction=rx bits=32 words=8 bytes=32 ready; start master now
[slave] case=14 direction=rx bits=32 words=8 result=match
[slave] case=15 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=15 direction=rx bits=8 words=8 result=match
[slave] case=16 direction=rx bits=16 words=8 bytes=16 ready; start master now
[slave] case=16 direction=rx bits=16 words=8 result=match
[slave] case=17 direction=rx bits=32 words=8 bytes=32 ready; start master now
[slave] case=17 direction=rx bits=32 words=8 result=match
[suite] 03_rearm         PASS: all cases passed
- **03_rearm**: **PASS** - all cases passed
[slave] case=18 direction=rx bits=8 words=128 bytes=128 ready; start master now
[slave] case=18 direction=rx bits=8 words=128 result=match
[slave] case=19 direction=rx bits=16 words=128 bytes=256 ready; start master now
[slave] case=19 direction=rx bits=16 words=128 result=match
[slave] case=20 direction=rx bits=32 words=128 bytes=512 ready; start master now
[slave] case=20 direction=rx bits=32 words=128 result=match
[slave] case=21 direction=rx bits=8 words=128 bytes=128 ready; start master now
[slave] case=21 direction=rx bits=8 words=128 result=match
[slave] case=22 direction=rx bits=16 words=128 bytes=256 ready; start master now
[slave] case=22 direction=rx bits=16 words=128 result=match
[slave] case=23 direction=rx bits=32 words=128 bytes=512 ready; start master now
[slave] case=23 direction=rx bits=32 words=128 result=match
[suite] 04_long_rx       PASS: all cases passed
- **04_long_rx**: **PASS** - all cases passed
[slave] case=24 direction=tx bits=8 words=8 bytes=8 ready; start master now
[slave] case=24 direction=tx bits=8 words=8 result=clocked
[slave] case=25 direction=tx bits=16 words=8 bytes=16 ready; start master now
[slave] case=25 direction=tx bits=16 words=8 result=clocked
[slave] case=26 direction=tx bits=32 words=8 bytes=32 ready; start master now
[slave] case=26 direction=tx bits=32 words=8 result=clocked
[suite] 05_slave_tx_widths PASS: all cases passed
- **05_slave_tx_widths**: **PASS** - all cases passed
[slave] case=27 direction=tx bits=8 words=1 bytes=1 ready; start master now
[slave] case=27 direction=tx bits=8 words=1 result=clocked
[slave] case=28 direction=tx bits=16 words=1 bytes=2 ready; start master now
[slave] case=28 direction=tx bits=16 words=1 result=clocked
[slave] case=29 direction=tx bits=32 words=1 bytes=4 ready; start master now
[slave] case=29 direction=tx bits=32 words=1 result=clocked
[suite] 06_slave_tx_min  PASS: all cases passed
- **06_slave_tx_min**: **PASS** - all cases passed
[slave] case=30 direction=tx bits=8 words=64 bytes=64 ready; start master now
[slave] case=30 direction=tx bits=8 words=64 result=clocked
[slave] case=31 direction=tx bits=16 words=64 bytes=128 ready; start master now
[slave] case=31 direction=tx bits=16 words=64 result=clocked
[slave] case=32 direction=tx bits=32 words=64 bytes=256 ready; start master now
[slave] case=32 direction=tx bits=32 words=64 result=clocked
[suite] 07_slave_tx_fifo_limit PASS: all cases passed
- **07_slave_tx_fifo_limit**: **PASS** - all cases passed
[slave] case=33 direction=tx bits=8 words=9 bytes=9 ready; start master now
[slave] case=33 direction=tx bits=8 words=9 result=clocked
[slave] case=34 direction=tx bits=16 words=9 bytes=18 ready; start master now
[slave] case=34 direction=tx bits=16 words=9 result=clocked
[slave] case=35 direction=tx bits=32 words=9 bytes=36 ready; start master now
[slave] case=35 direction=tx bits=32 words=9 result=clocked
[suite] 08_slave_tx_append_zero PASS: all cases passed
- **08_slave_tx_append_zero**: **PASS** - all cases passed
[slave] case=36 direction=duplex bits=8 words=8 bytes=8 ready; start master now
[slave] case=36 direction=duplex bits=8 words=8 result=match
[slave] case=37 direction=duplex bits=16 words=8 bytes=16 ready; start master now
[slave] case=37 direction=duplex bits=16 words=8 result=match
[slave] case=38 direction=duplex bits=32 words=8 bytes=32 ready; start master now
[slave] case=38 direction=duplex bits=32 words=8 result=match
[suite] 09_duplex_widths PASS: all cases passed
- **09_duplex_widths**: **PASS** - all cases passed
[slave] case=39 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=39 direction=rx bits=8 words=8 result=match
[suite] 10_speed_100000Hz PASS: all cases passed
- **10_speed_100000Hz**: **PASS** - all cases passed
[slave] case=40 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=40 direction=rx bits=8 words=8 result=match
[suite] 10_speed_1000000Hz PASS: all cases passed
- **10_speed_1000000Hz**: **PASS** - all cases passed
[slave] case=41 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=41 direction=rx bits=8 words=8 result=match
[suite] 10_speed_10000000Hz PASS: all cases passed
- **10_speed_10000000Hz**: **PASS** - all cases passed
[slave] case=42 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=42 direction=rx bits=8 words=8 result=match
[suite] 10_speed_25000000Hz PASS: all cases passed
- **10_speed_25000000Hz**: **PASS** - all cases passed
[slave] case=43 direction=rx bits=8 words=8 bytes=8 ready; start master now
[slave] case=43 direction=rx bits=8 words=8 result=match
[suite] 10_speed_40000000Hz PASS: all cases passed
- **10_speed_40000000Hz**: **PASS** - all cases passed
[suite] overall          PASS: all ten groups passed
- **overall**: **PASS** - all ten groups passed

Result: PASS (pass=15 fail=0)
