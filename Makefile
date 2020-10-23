# dk (de·cay) window manager
# see license file for copyright and license details

VERSION = 0.85

# install paths
PREFIX ?= /usr/local
MAN    ?= ${PREFIX}/share/man
DOC    ?= ${PREFIX}/share/doc

# compiler and linker flags
CPPFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE -DVERSION=\"${VERSION}\"
CFLAGS   += -std=c11 -O2 -static -pedantic -Wall -Wextra
LDFLAGS  ?=

VPATH = src

all: dk dkcmd

debug: CPPFLAGS += -DDEBUG
debug: all

coverage: CFLAGS += --coverage
coverage: LDLIBS += -lgcov
coverage: all

nostrip: CFLAGS += -g -O0
nostrip: debug

dk: LDLIBS := -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr
dk: dk.o
dkcmd: LDLIBS :=
dkcmd: dkcmd.o

dk.o: %.o: %.c
	test -f src/config.h || cp src/config.def.h src/config.h
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

dkcmd.o: %.o: %.c
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

clean:
	rm -f *.o dk dkcmd

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -Dm755 dk ${DESTDIR}${PREFIX}/bin/
	install -Dm755 dkcmd ${DESTDIR}${PREFIX}/bin/
	mkdir -p ${DESTDIR}${MAN}/man1
	sed "s/VERSION/${VERSION}/g" man/dk.1 > ${DESTDIR}${MAN}/man1/dk.1
	cp -rfp man/dkcmd.1 ${DESTDIR}${MAN}/man1/dkcmd.1
	chmod 644 ${DESTDIR}${MAN}/man1/dk.1
	chmod 644 ${DESTDIR}${MAN}/man1/dkcmd.1
	mkdir -p ${DESTDIR}${DOC}/dk
	cp -rf doc/* ${DESTDIR}${DOC}/dk

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dk
	rm -f ${DESTDIR}${PREFIX}/bin/dkcmd
	rm -f ${DESTDIR}${MAN}/man1/dk.1
	rm -f ${DESTDIR}${MAN}/man1/dkcmd.1
	rm -rf ${DESTDIR}${DOC}/dk

.PHONY: all debug nostrip clean install uninstall
