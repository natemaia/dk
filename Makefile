# dk /dəˈkā/ window manager
# see license file for copyright and license details

VERSION = 1.0

# install paths
VPATH   = src
PREFIX ?= /usr/local
MAN    ?= ${PREFIX}/share/man
DOC    ?= ${PREFIX}/share/doc
SES    ?= /usr/share/xsessions

# source and object files
SRC  = dk.c cmd.c event.c layout.c parse.c strl.c util.c
OBJ  = ${SRC:.c=.o}
CSRC = dkcmd.c strl.c util.c
COBJ = ${CSRC:.c=.o}


# compiler and linker flags
OPTLVL = -O2

CPPFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE -DVERSION=\"${VERSION}\"
CFLAGS   += -flto -std=c99 -pedantic -Wall -Wextra -I/usr/X11R6/include
LDFLAGS   = -flto -L/usr/X11R6/lib -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr

all: dk dkcmd

# debug: OPTLVL = -Og
debug: CPPFLAGS += -DDEBUG
debug: all

funcdebug: CPPFLAGS += -DFUNCDEBUG
funcdebug: debug

nostrip: CFLAGS += -g
nostrip: debug

.c.o:
	${CC} ${OPTLVL} ${CFLAGS} ${CPPFLAGS} -c $<

${OBJ}: config.h

config.h:
	@test -e src/$@ || cp -v src/config.def.h src/$@

dk: config.h ${OBJ}
	${CC} ${OBJ} -o $@ ${LDFLAGS}

dkcmd: ${COBJ}
	${CC} ${COBJ} -o $@

clean:
	rm -f *.o dk dkcmd

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${SES} ${DESTDIR}${MAN}/man1 ${DESTDIR}${DOC}/dk
	install -Dm755 dk dkcmd ${DESTDIR}${PREFIX}/bin/
	sed "s/VERSION/${VERSION}/g" man/dk.1 > ${DESTDIR}${MAN}/man1/dk.1
	cp -rfp man/dkcmd.1 ${DESTDIR}${MAN}/man1/dkcmd.1
	chmod 644 ${DESTDIR}${MAN}/man1/dk.1 ${DESTDIR}${MAN}/man1/dkcmd.1
	cp -rf doc/* ${DESTDIR}${DOC}/dk
	install -Dm644 dk.desktop ${DESTDIR}${SES}/

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dk ${DESTDIR}${PREFIX}/bin/dkcmd
	rm -f ${DESTDIR}${MAN}/man1/dk.1 ${DESTDIR}${MAN}/man1/dkcmd.1
	rm -rf ${DESTDIR}${DOC}/dk
	rm -f ${DESTDIR}${SES}/dk.desktop

.PHONY: all debug funcdebug nostrip clean install uninstall
