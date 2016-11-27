VERSION = 0.1.1
DESTDIR ?= /usr/local
CFLAGS += -ggdb -Wall -Wextra -DVERSION=\"${VERSION}\" -D_GNU_SOURCE

all: brightnessctl

install: brightnessctl
	install -d ${DESTDIR}/bin
	install -m 4755 -p brightnessctl ${DESTDIR}/bin/

clean:
	rm -f brightnessctl

.PHONY: all install clean
