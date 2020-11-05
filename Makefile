# dk /dəˈkā/ window manager
# see license file for copyright and license details

VERSION = 1.0

# install paths
VPATH   = src
PREFIX ?= /usr/local
MAN    ?= ${PREFIX}/share/man
DOC    ?= ${PREFIX}/share/doc
SES    ?= ${PREFIX}/share/xsessions

# compiler and linker flags
CPPFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE -DVERSION=\"${VERSION}\"
CFLAGS   += -std=c11 -O2 -static -pedantic -Wall -Wextra

all: dk dkcmd

debug: CPPFLAGS += -DDEBUG
debug: all

nostrip: CFLAGS += -g -O0
nostrip: debug

dk: CFLAGS  += -I/usr/X11R6/include
dk: LDLIBS  := -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr
dk: LDFLAGS ?= -L/usr/X11R6/lib
dk: dk.o
dk.o: config.h
dkcmd: LDLIBS :=
dkcmd: dkcmd.o

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

config.h:
	cp src/config.def.h src/$@

clean:
	rm -f *.o dk dkcmd

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${SES} ${DESTDIR}${MAN}/man1 ${DESTDIR}${DOC}/dk
	install -Dm755 dk ${DESTDIR}${PREFIX}/bin/
	install -Dm755 dkcmd ${DESTDIR}${PREFIX}/bin/
	mkdir -p ${DESTDIR}${MAN}/man1
	sed "s/VERSION/${VERSION}/g" man/dk.1 > ${DESTDIR}${MAN}/man1/dk.1
	cp -rfp man/dkcmd.1 ${DESTDIR}${MAN}/man1/dkcmd.1
	chmod 644 ${DESTDIR}${MAN}/man1/dk.1
	chmod 644 ${DESTDIR}${MAN}/man1/dkcmd.1
	mkdir -p ${DESTDIR}${DOC}/dk
	cp -rf doc/* ${DESTDIR}${DOC}/dk
	install -Dm644 dk.desktop ${DESTDIR}${SES}/

release: clean
	mkdir -p dk-${VERSION}
	cp -R LICENSE Makefile README.md src man doc dk-${VERSION}
	tar -cf dk-${VERSION}.tar dk-${VERSION}
	gzip dk-${VERSION}.tar
	rm -rf dk-${VERSION}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dk
	rm -f ${DESTDIR}${PREFIX}/bin/dkcmd
	rm -f ${DESTDIR}${MAN}/man1/dk.1
	rm -f ${DESTDIR}${MAN}/man1/dkcmd.1
	rm -rf ${DESTDIR}${DOC}/dk
	rm -f ${DESTDIR}${PREFIX}/share/xsessions/dk.desktop

.PHONY: all debug nostrip clean install uninstall
