VERSION = 0.1

# Paths
prefix	= $(HOME)
BINDIR	= $(prefix)/bin
DESTDIR	=

CC := gcc
CFLAGS := -Wall -D_USE_SOURCE

DEBUG = false

ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -g -DDEBUG
endif

PROGRAMS := inotail inotail-old inotify-watchdir

all: inotail
inotail: inotail.o
inotail-old: inotail-old.o
inotify-watchdir: inotify-watchdir.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: inotail
	install -m 775 inotail $(DESTDIR)$(BINDIR)

cscope:
	cscope -b

release:
	git-tar-tree HEAD inotail-$(VERSION) | gzip -9v > ../inotail-$(VERSION).tar.gz
	git-tar-tree HEAD inotail-$(VERSION) | bzip2 -9v > ../inotail-$(VERSION).tar.bz2

clean:
	rm -f *.o
	rm -f $(PROGRAMS)
	rm -f cscope.out
