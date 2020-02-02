# installation base directory
PREFIX = /usr

# compiler and linker flags
CC = cc
CCFLAGS = -O3 -Wall -Wextra -Wno-missing-field-initializers
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm -lxcb-randr
