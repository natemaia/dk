# yaxwm - yet another x window manager

# installation base directory
PREFIX = /usr

# compiler and linker flags
CC = cc
CCFLAGS = -O3 -Wall -Wextra
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr

# is this a debug build (make -DDEBUG ...)
ifneq ($(findstring DEBUG,$(DFLAGS)),)
	LDFLAGS += -lxkbcommon
endif

ifneq ($(findstring NOSTRIP,$(DFLAGS)),)
	CCFLAGS += -g
endif

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
