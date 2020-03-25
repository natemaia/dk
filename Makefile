# yaxwm - yet another x window manager
# see license file for copyright and license details

VERSION = 0.3

# install paths
PREFIX    ?= /usr
MANPREFIX ?= ${PREFIX}/share/man
DOCPREFIX ?= ${PREFIX}/share/doc

# compiler and linker flags
CPPFLAGS += -DVERSION=\"${VERSION}\"
CFLAGS   += -O2 -pedantic -Wall -Wextra
LDFLAGS  ?=
LDLIBS    = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr

VPATH = src

all: yaxwm yaxcmd

debug: CPPFLAGS += -DDEBUG
debug: all

nostrip: CFLAGS += -g -O0
nostrip: debug

yaxwm: yaxwm.o
yaxcmd: yaxcmd.o

yaxwm.o: %.o: %.c
	@test -f ${VPATH}/config.h || cp ${VPATH}/config.def.h ${VPATH}/config.h
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

yaxcmd.o: %.o: %.c
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

clean:
	rm -f yaxwm yaxcmd yaxwm.o yaxcmd.o

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f yaxwm yaxcmd ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/yaxcmd ${DESTDIR}${PREFIX}/bin/yaxwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" man/yaxwm.1 > ${DESTDIR}${MANPREFIX}/man1/yaxwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/yaxwm.1
	mkdir -p ${DESTDIR}${DOCPREFIX}/yaxwm
	cp -rfp doc ${DESTDIR}${DOCPREFIX}/yaxwm

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/yaxwm ${DESTDIR}${PREFIX}/bin/yaxcmd \
		${DESTDIR}${MANPREFIX}/man1/yaxwm.1 ${DESTDIR}${DOCPREFIX}/yaxwm

.PHONY: all debug nostrip clean install uninstall
