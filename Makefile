V=1
VDEVEL=$(shell test -d .git && git describe 2>/dev/null)

ifneq "$(VDEVEL)" ""
V=$(VDEVEL)
endif

CC := $(CC) -std=c99

base_CFLAGS = -Wall -Wextra -pedantic -O2 -g
base_LIBS = -lm

pkgs = vte-2.91
pkgs_CFLAGS = $(shell pkg-config --cflags $(pkgs))
pkgs_LIBS = $(shell pkg-config --libs $(pkgs))

CPPFLAGS += -DMINITERM_VERSION=\"$(V)\"
CFLAGS := $(base_CFLAGS) $(pkgs_CFLAGS) $(CFLAGS)
LDLIBS := $(base_LIBS) $(pkgs_LIBS)

all: miniterm

miniterm: miniterm.c config.h

clean:
	$(RM) miniterm miniterm.o

install: miniterm
	install -Dm755 miniterm $(DESTDIR)/usr/bin/miniterm
