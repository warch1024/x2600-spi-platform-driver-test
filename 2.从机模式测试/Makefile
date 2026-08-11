CROSS_COMPILE ?= /home/devvean/work/linux/buildroot/buildroot24/output/host/bin/mips-linux-gnu-
TARGET_CC ?= $(CROSS_COMPILE)gcc
HOST_CC ?= cc
APP_INC ?= /home/devvean/work/linux/libhardware2/include
APP_LIB ?= /home/devvean/work/linux/buildroot/buildroot24/output/target/usr/lib
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99

all: spi_endian_master spi_endian_slave

spi_endian_master: spi_endian_master.c pattern.c pattern.h

	$(TARGET_CC) $(CFLAGS) -I. spi_endian_master.c pattern.c -o $@

spi_endian_slave: spi_endian_slave.c pattern.c pattern.h

	$(TARGET_CC) $(CFLAGS) -I. -I$(APP_INC) spi_endian_slave.c pattern.c -L$(APP_LIB) -lhardware2 -lutils2 -Wl,-rpath-link,$(APP_LIB) -o $@

test: tests/test_pattern.c tests/test_master_options.sh tests/test_master_append_zero.sh \
	  tests/test_slave_config.sh tests/test_slave_suite.sh tests/master_ioctl_stub.c \
	  tests/sslv_stub.c tests/stubs/libhardware2/sslv.h pattern.c pattern.h \
	  spi_endian_master.c spi_endian_slave.c

	$(HOST_CC) $(CFLAGS) -I. tests/test_pattern.c pattern.c -o /tmp/test_spi_endian_pattern
	/tmp/test_spi_endian_pattern
	$(HOST_CC) $(CFLAGS) -I. spi_endian_master.c pattern.c -o /tmp/test_spi_endian_master
	sh tests/test_master_options.sh /tmp/test_spi_endian_master
	$(HOST_CC) $(CFLAGS) -I. spi_endian_master.c pattern.c tests/master_ioctl_stub.c \
		-Wl,--wrap=ioctl -Wl,--wrap=malloc -o /tmp/test_spi_endian_master_append_zero
	sh tests/test_master_append_zero.sh /tmp/test_spi_endian_master_append_zero
	$(HOST_CC) $(CFLAGS) -Itests/stubs -I. spi_endian_slave.c pattern.c tests/sslv_stub.c -o /tmp/test_spi_endian_slave
	sh tests/test_slave_config.sh /tmp/test_spi_endian_slave
	sh tests/test_slave_suite.sh /tmp/test_spi_endian_slave

clean:
	rm -f spi_endian_master spi_endian_slave /tmp/test_spi_endian_pattern /tmp/test_spi_endian_master /tmp/test_spi_endian_master_append_zero /tmp/test_spi_endian_slave

.PHONY: all test clean
