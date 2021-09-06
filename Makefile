VERSION = 0.5
CFLAGS += -std=c99 -g -Wall -Weverything -DVERSION=\"${VERSION}\" -D_POSIX_C_SOURCE=200809L
LDLIBS = -lm

PREFIX ?= /usr
BINDIR = ${DESTDIR}${PREFIX}/bin
MANDIR = ${DESTDIR}${PREFIX}/share/man

INSTALL_UDEV_RULES = 1

INSTALL_UDEV_1 = install_udev_rules
UDEVDIR ?= /lib/udev/rules.d

MODE_0 = 4711
MODE_1 = 0755
MODE = ${MODE_${INSTALL_UDEV_RULES}}

ifdef ENABLE_SYSTEMD
	CFLAGS += ${shell pkg-config --cflags libsystemd}
	LDLIBS += ${shell pkg-config --libs libsystemd}
	CPPFLAGS += -DENABLE_SYSTEMD
	INSTALL_UDEV_RULES=0
	MODE = 0755
endif

all: brightnessctl brightnessctl.1

install: all ${INSTALL_UDEV_${INSTALL_UDEV_RULES}}
	install -d ${BINDIR} ${MANDIR}/man1
	install -m ${MODE} brightnessctl   ${BINDIR}/
	install -m 0644    brightnessctl.1 ${MANDIR}/man1

install_udev_rules:
	install -d ${DESTDIR}${UDEVDIR}
	install -m 0644 90-brightnessctl.rules ${DESTDIR}${UDEVDIR}

clean:
	rm -f brightnessctl

.PHONY: all install clean
