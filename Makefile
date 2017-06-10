VERSION = 0.2
DESTDIR ?= /usr/local
CFLAGS += -std=c99 -g -Wall -Wextra -DVERSION=\"${VERSION}\" -D_POSIX_C_SOURCE=200809L
MODE ?= 4711

all: brightnessctl

install: brightnessctl
	install -d ${DESTDIR}/bin
	install -m ${MODE} brightnessctl ${DESTDIR}/bin/

clean:
	rm -f brightnessctl

.PHONY: all install clean
