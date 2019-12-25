# yaxwm - yet another x window manager

PREFIX = /usr/local

CC = cc
CCFLAGS = -Wall -Wextra -Wno-missing-field-initializers -O3
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr -lxcb-ewmh

all: yaxwm

config.h:
	cp config.def.h $@

yaxwm: config.h
	${CC} ${DFLAGS} yaxwm.c ${CCFLAGS} -o $@ ${LDFLAGS}

clean:
	rm -f yaxwm

install: all
	cp -f yaxwm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/yaxwm

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/yaxwm

.PHONY: all clean install uninstall
