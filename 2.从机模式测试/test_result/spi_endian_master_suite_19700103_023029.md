# SPI SSLV Master Suite Report

## Results

[suite] report=./spi_endian_master_suite_19700103_023029.md
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
[master] case=18 direction=rx bits=16 words=128 bytes=256 sent; cs released
[master] case=19 direction=rx bits=16 words=128 bytes=256 sent; cs released
[suite] 04_long_rx       PASS: all cases passed
- **04_long_rx**: **PASS** - all cases passed
[master] case=20 direction=tx bits=16 words=8 bytes=16 sent; cs released miso=match
[master] case=21 direction=tx bits=16 words=8 bytes=16 sent; cs released miso=match
[suite] 05_slave_tx      PASS: all cases passed
- **05_slave_tx**: **PASS** - all cases passed
[master] case=22 direction=duplex bits=16 words=8 bytes=16 sent; cs released miso=match
[master] case=23 direction=duplex bits=16 words=8 bytes=16 sent; cs released miso=match
[suite] 06_duplex        PASS: all cases passed
- **06_duplex**: **PASS** - all cases passed
[master] case=24 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 07_speed_100000Hz PASS: all cases passed
- **07_speed_100000Hz**: **PASS** - all cases passed
[master] case=25 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 07_speed_1000000Hz PASS: all cases passed
- **07_speed_1000000Hz**: **PASS** - all cases passed
[master] case=26 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 07_speed_10000000Hz PASS: all cases passed
- **07_speed_10000000Hz**: **PASS** - all cases passed
[master] case=27 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 07_speed_25000000Hz PASS: all cases passed
- **07_speed_25000000Hz**: **PASS** - all cases passed
[master] case=28 direction=rx bits=8 words=8 bytes=8 sent; cs released
[suite] 07_speed_40000000Hz PASS: all cases passed
- **07_speed_40000000Hz**: **PASS** - all cases passed
[suite] overall          PASS: all seven groups passed
- **overall**: **PASS** - all seven groups passed

Result: PASS (pass=12 fail=0)
