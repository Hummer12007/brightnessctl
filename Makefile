VERSION = 0.3
CFLAGS += -std=c99 -g -Wall -Wextra -DVERSION=\"${VERSION}\" -D_POSIX_C_SOURCE=200809L -lm

PREFIX ?= /usr
BINDIR = ${DESTDIR}${PREFIX}/bin

INSTALL_UDEV_RULES = 0

INSTALL_UDEV_1 = install_udev_rules
UDEVDIR ?= /lib/udev/rules.d

MODE_0 = 4711
MODE_1 = 0644
MODE = ${MODE_${INSTALL_UDEV_RULES}}

all: brightnessctl

install: brightnessctl ${INSTALL_UDEV_${INSTALL_UDEV_RULES}}
	install -d ${DESTDIR}${PREFIX}/bin
	install -m ${MODE} brightnessctl ${DESTDIR}${PREFIX}/bin/

install_udev_rules:
	install -d ${DESTDIR}${UDEVDIR}
	install -m 0644 90-brightnessctl.rules ${DESTDIR}${UDEVDIR}


clean:
	rm -f brightnessctl

.PHONY: all install clean
