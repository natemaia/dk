# yaxwm - yet another x window manager
# see license file for copyright and license details

VERSION = 0.84

# install paths
PREFIX    ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
DOCPREFIX ?= ${PREFIX}/share/doc

# compiler and linker flags
CPPFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE -DVERSION=\"${VERSION}\"
CFLAGS   += -std=c11 -O2 -static -pedantic -Wall -Wextra
LDFLAGS  ?=
LDLIBS    = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr

all: yaxwm

debug: CPPFLAGS += -DDEBUG
debug: all

coverage: CFLAGS += --coverage
coverage: LDLIBS += -lgcov
coverage: all

nostrip: CFLAGS += -g -O0
nostrip: debug

yaxwm: yaxwm.o

yaxwm.o: %.o: %.c
	test -f yaxwm.h || cp yaxwm.def.h yaxwm.h
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

clean:
	rm -f yaxwm yaxwm.o

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -Dm755 yaxwm ${DESTDIR}${PREFIX}/bin/
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" man/yaxwm.1 > ${DESTDIR}${MANPREFIX}/man1/yaxwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/yaxwm.1
	mkdir -p ${DESTDIR}${DOCPREFIX}/yaxwm
	cp -rf doc/* ${DESTDIR}${DOCPREFIX}/yaxwm

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/yaxwm
	rm -f ${DESTDIR}${MANPREFIX}/man1/yaxwm.1
	rm -rf ${DESTDIR}${DOCPREFIX}/yaxwm

.PHONY: all debug nostrip clean install uninstall
