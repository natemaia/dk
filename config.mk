# installation base directory
PREFIX = /usr

# compiler and linker flags
CC = cc
CCFLAGS = -O3 -Wall -Wextra
LDFLAGS = -lxcb -lxcb-keysyms -lxcb-util -lxcb-cursor -lxcb-icccm

# RANDR extension for multi-head, comment to disable
LDFLAGS += -lxcb-randr
DDFLAGS += -DRANDR

# ewmh dock/panel/taskbar support, comment to disable
DDFLAGS += -DPANEL
