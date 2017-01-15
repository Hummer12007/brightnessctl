VERSION = 0.1.1
DESTDIR ?= /usr/local
CFLAGS += -std=c99 -g -Wall -Wextra -DVERSION=\"${VERSION}\" -D_GNU_SOURCE
MODE ?= 4711

all: brightnessctl

install: brightnessctl
	install -d ${DESTDIR}/bin
	install -m ${MODE} brightnessctl ${DESTDIR}/bin/

clean:
	rm -f brightnessctl

.PHONY: all install clean
