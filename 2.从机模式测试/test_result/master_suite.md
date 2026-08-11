# SPI SSLV Master Suite Report

## Results

[suite] report=/tmp/master_suite.md
[master] suite device=/dev/spidev1.0; start slave suite first
[suite] 01_config        PASS: mode=3, MSB-first, bpw=8 accepted
- **01_config**: **PASS** - mode=3, MSB-first, bpw=8 accepted
[master] case=0 direction=rx bits=8 words=8 bytes=8 sent; cs released
[master] case=1 direction=rx bits=16 words=8 bytes=16 sent; cs released
[master] case=2 direction=rx bits=32 words=8 bytes=32 sent; cs released
[suite] 02_basic_rx      PASS: all cases passed
- **02_basic_rx**: **PASS** - all cases passed
[master] case=3 direction=rx bits=8 words=8 bytes=8 sent; cs released
[master] case=4 direction=rx bits=16 words=8 bytes=16 sent; cs released
[master] case=5 direction=rx bits=32 words=8 bytes=32 sent; cs released
[master] case=6 direction=rx bits=8 words=8 bytes=8 sent; cs released
[master] case=7 direction=rx bits=16 words=8 bytes=16 sent; cs released
[master] case=8 direction=rx bits=32 words=8 bytes=32 sent; cs released
[master] case=9 direction=rx bits=8 words=8 bytes=8 sent; cs released
[master] case=10 direction=rx bits=16 words=8 bytes=16 sent; cs released
[master] case=11 direction=rx bits=32 words=8 bytes=32 sent; cs released
[master] case=12 direction=rx bits=8 words=8 bytes=8 sent; cs released
[master] case=13 direction=rx bits=16 words=8 bytes=16 sent; cs released
[master] case=14 direction=rx bits=32 words=8 bytes=32 sent; cs released
[master] case=15 direction=rx bits=8 words=8 bytes=8 sent; cs released
[master] case=16 direction=rx bits=16 words=8 bytes=16 sent; cs released
[master] case=17 direction=rx bits=32 words=8 bytes=32 sent; cs released
[suite] 03_rearm         PASS: all cases passed
- **03_rearm**: **PASS** - all cases passed
[master] case=18 direction=rx bits=8 words=128 bytes=128 sent; cs released
[master] case=19 direction=rx bits=16 words=128 bytes=256 sent; cs released
[master] case=20 direction=rx bits=32 words=128 bytes=512 sent; cs released
[master] case=21 direction=rx bits=8 words=128 bytes=128 sent; cs released
[master] case=22 direction=rx bits=16 words=128 bytes=256 sent; cs released
[master] case=23 direction=rx bits=32 words=128 bytes=512 sent; cs released
[suite] 04_long_rx       PASS: all cases passed
- **04_long_rx**: **PASS** - all cases passed
[master] case=24 direction=tx bits=8 words=8 bytes=8 sent; cs released miso=match
[master] case=25 direction=tx bits=16 words=8 bytes=16 sent; cs released miso=match
[master] case=26 direction=tx bits=32 words=8 bytes=32 sent; cs released miso=match
[suite] 05_slave_tx_widths PASS: all cases passed
- **05_slave_tx_widths**: **PASS** - all cases passed
[master] case=27 direction=tx bits=8 words=1 bytes=1 sent; cs released miso=match
[master] case=28 direction=tx bits=16 words=1 bytes=2 sent; cs released miso=match
[master] case=29 direction=tx bits=32 words=1 bytes=4 sent; cs released miso=match
[suite] 06_slave_tx_min  PASS: all cases passed
- **06_slave_tx_min**: **PASS** - all cases passed
[master] case=30 direction=tx bits=8 words=64 bytes=64 sent; cs released miso=match
[master] case=31 direction=tx bits=16 words=64 bytes=128 sent; cs released miso=match
[master] case=32 direction=tx bits=32 words=64 bytes=256 sent; cs released miso=match
[suite] 07_slave_tx_fifo_limit PASS: all cases passed
- **07_slave_tx_fifo_limit**: **PASS** - all cases passed
[master] case=33 direction=tx bits=8 words=9 bytes=9 sent; cs released miso=match
[master] case=34 direction=tx bits=16 words=9 bytes=18 sent; cs released miso=match
[master] case=35 direction=tx bits=32 words=9 bytes=36 sent; cs released miso=match
[suite] 08_slave_tx_append_zero PASS: all cases passed
- **08_slave_tx_append_zero**: **PASS** - all cases passed
[master] case=36 direction=duplex bits=8 words=8 bytes=8 sent; cs released miso=match
[master] case=37 direction=duplex bits=16 words=8 bytes=16 sent; cs released miso=match
[master] case=38 direction=duplex bits=32 words=8 bytes=32 sent; cs released miso=match
[suite] 09_duplex_widths PASS: all cases passed
- **09_duplex_widths**: **PASS** - all cases passed
[master] case=39 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 10_speed_100000Hz PASS: all cases passed
- **10_speed_100000Hz**: **PASS** - all cases passed
[master] case=40 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 10_speed_1000000Hz PASS: all cases passed
- **10_speed_1000000Hz**: **PASS** - all cases passed
[master] case=41 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 10_speed_10000000Hz PASS: all cases passed
- **10_speed_10000000Hz**: **PASS** - all cases passed
[master] case=42 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 10_speed_25000000Hz PASS: all cases passed
- **10_speed_25000000Hz**: **PASS** - all cases passed
[master] case=43 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 10_speed_40000000Hz PASS: all cases passed
- **10_speed_40000000Hz**: **PASS** - all cases passed
[suite] overall          PASS: all ten groups passed
- **overall**: **PASS** - all ten groups passed

Result: PASS (pass=15 fail=0)
