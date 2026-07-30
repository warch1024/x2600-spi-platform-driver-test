APP_CC ?= /home/devvean/work/linux/buildroot/buildroot24/output/host/bin/mips-linux-gnu-gcc
APP_INC ?= /home/devvean/work/linux/libhardware2/include
APP_LIB ?= /home/devvean/work/linux/buildroot/buildroot24/output/target/usr/lib
UTILS_LIB ?= /home/devvean/work/linux/buildroot/buildroot24/output/target/usr/lib
APP_CFLAGS ?= -O2 -Wall -Wextra -std=gnu99

app: app_spi.c
	$(APP_CC) $(APP_CFLAGS) -I$(APP_INC) app_spi.c -L$(APP_LIB) -L$(UTILS_LIB) -lhardware2 -lutils2 -Wl,-rpath-link,$(UTILS_LIB) -o app_spi

.PHONY: app
