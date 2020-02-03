# see license file for copyright and license details

VERSION = 0.1

# installation paths
PREFIX = /usr
MANPREFIX = ${PREFIX}/share/man

# compiler and linker flags
CC = cc
CFLAGS = -O3 -Wall
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr
