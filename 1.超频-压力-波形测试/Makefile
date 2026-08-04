APP_CC ?= /home/devvean/work/linux/buildroot/buildroot24/output/host/bin/mips-linux-gnu-gcc
APP_INC ?= /home/devvean/work/linux/libhardware2/include
APP_LIB ?= /home/devvean/work/linux/buildroot/buildroot24/output/target/usr/lib
UTILS_LIB ?= /home/devvean/work/linux/buildroot/buildroot24/output/target/usr/lib
APP_CFLAGS ?= -O2 -Wall -Wextra -std=gnu99
HOST_CC ?= cc
APP_SOURCES := app_spi.c spi_stats.c

app: $(APP_SOURCES) spi_stats.h spi_clock.h spi_stress.h spi_timing.h
	$(APP_CC) $(APP_CFLAGS) -I$(APP_INC) $(APP_SOURCES) -L$(APP_LIB) -L$(UTILS_LIB) -lhardware2 -lutils2 -lm -Wl,-rpath-link,$(UTILS_LIB) -o app_spi

test: spi_stats.c spi_stats.h spi_timing.h spi_stress.h tests/test_spi_stats.c tests/test_spi_clock.c tests/test_spi_timing.c tests/test_spi_stress.c
	$(HOST_CC) -std=gnu99 -Wall -Wextra -I. tests/test_spi_stats.c spi_stats.c -lm -o /tmp/test_spi_stats
	/tmp/test_spi_stats
	$(HOST_CC) -std=gnu99 -Wall -Wextra -I. tests/test_spi_clock.c -o /tmp/test_spi_clock
	/tmp/test_spi_clock
	$(HOST_CC) -std=gnu99 -Wall -Wextra -I. tests/test_spi_timing.c -lm -o /tmp/test_spi_timing
	/tmp/test_spi_timing
	$(HOST_CC) -std=gnu99 -Wall -Wextra -I. tests/test_spi_stress.c -o /tmp/test_spi_stress
	/tmp/test_spi_stress

.PHONY: app test
