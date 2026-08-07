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

test: tests/test_pattern.c tests/test_master_options.sh pattern.c pattern.h spi_endian_master.c

	$(HOST_CC) $(CFLAGS) -I. tests/test_pattern.c pattern.c -o /tmp/test_spi_endian_pattern
	/tmp/test_spi_endian_pattern
	$(HOST_CC) $(CFLAGS) -I. spi_endian_master.c pattern.c -o /tmp/test_spi_endian_master
	sh tests/test_master_options.sh /tmp/test_spi_endian_master

clean:
	rm -f spi_endian_master spi_endian_slave /tmp/test_spi_endian_pattern /tmp/test_spi_endian_master

.PHONY: all test clean
