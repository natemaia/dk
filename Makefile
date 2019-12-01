# yaswm - yet another simple window manager

PREFIX = /usr
MANPREFIX = ${PREFIX}/share/man

LDFLAGS = -lX11 -lxcb -lxcb-keysyms -lxcb-util # -lxcb-randr -lxcb-icccm -lxcb-ewmh -lxcb-aux

all: yaswm

yaswm:
	cc yaswm.c -Wall -Os -o yaswm ${LDFLAGS}

clean:
	rm -f yaswm

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f yaswm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/dwm

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/yaswm

.PHONY: all clean install uninstall
