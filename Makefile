VERSION = 0.1.1
DESTDIR ?= /usr/local
CFLAGS += -ggdb -Wall -Wextra -DVERSION=\"${VERSION}\" -D_GNU_SOURCE
LDFLAGS ?=

all: brightnessctl

install: brightnessctl
	install -d ${DESTDIR}/bin
	install -m 4711 brightnessctl ${DESTDIR}/bin/

clean:
	rm -f brightnessctl

.PHONY: all install clean
