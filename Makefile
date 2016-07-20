all: brightnessctl
VERSION=0.1.1
DESTDIR=/usr/local
CFLAGS=-ggdb -Wall -Wextra -DVERSION=\"${VERSION}\" -D_GNU_SOURCE

install: brightnessctl
	install -m 4711 brightnessctl ${DESTDIR}/bin

clean:
	rm -f brightnessctl

.PHONY: all install clean
