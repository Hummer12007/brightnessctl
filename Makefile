VERSION = 0.2
DESTDIR ?= /usr/local
CFLAGS += -std=c99 -g -Wall -Wextra -DVERSION=\"${VERSION}\" -D_POSIX_C_SOURCE=200809L -lm
MODE ?= 4711

all: brightnessctl

install: brightnessctl
	install -d ${DESTDIR}/bin
	install -m ${MODE} brightnessctl ${DESTDIR}/bin/
	install -d ${DESTDIR}/lib/udev/rules.d
	install -m 0644 90-brightnessctl.rules ${DESTDIR}/lib/udev/rules.d/

clean:
	rm -f brightnessctl

.PHONY: all install clean
