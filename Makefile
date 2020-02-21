# yaxwm - yet another x window manager
# see license file for copyright and license details

VERSION = 0.2

# installation paths
PREFIX = /usr
MANPREFIX = ${PREFIX}/share/man

# compiler and linker flags
CC = cc
CFLAGS = -O3 -Wall -DVERSION=\"${VERSION}\"
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr


# debug build (make DFLAGS=' -DDEBUG ...')
ifneq ($(findstring DEBUG,$(DFLAGS)),)
	LDFLAGS += -lxkbcommon
	CFLAGS += -Wextra -Wno-missing-field-initializers
endif

# no strip build (make DFLAGS='-DNOSTRIP ...')
ifneq ($(findstring NOSTRIP,$(DFLAGS)),)
	CFLAGS += -g
endif

SRC = yaxwm.c
OBJ := ${SRC:.c=.o}
CFLAGS += ${DFLAGS}

all: yaxwm

config.h:
	cp config.def.h $@

${OBJ}: config.h

yaxwm: ${OBJ}

clean:
	rm -f yaxwm ${OBJ}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f yaxwm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/yaxwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" yaxwm.1 > ${DESTDIR}${MANPREFIX}/man1/yaxwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/yaxwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/yaxwm ${DESTDIR}${MANPREFIX}/man1/yaxwm.1

.PHONY: all clean install uninstall
