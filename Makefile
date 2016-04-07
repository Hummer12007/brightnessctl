all: brightnessctl
VERSION=0.1
PREFIX=/usr/local
CFLAGS=-Wall -Wextra -DVERSION=\"${VERSION}\"

install: brightnessctl
	install -m 4711 brightnessctl ${PREFIX}/bin

clean:
	rm -f brightnessctl

.PHONY: all install clean
