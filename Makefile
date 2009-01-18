# Makefile for inotail
#
# Copyright (C) 2006-2008 Tobias Klauser <tklauser@distanz.ch>
#
# Licensed under the terms of the GNU General Public License; version 2 or later.

VERSION	= 0.6

# Paths
prefix	= /usr/local
BINDIR	= $(prefix)/bin
MANDIR	= $(prefix)/share/man/man1

CC	:= gcc
CFLAGS	:= $(CFLAGS) -pipe -D_USE_SOURCE -DVERSION="\"$(VERSION)\"" -W -Wall \
	   -Wextra -Wstrict-prototypes -Wsign-compare -Wshadow -Wchar-subscripts \
	   -Wmissing-declarations -Wpointer-arith -Wcast-align -Wmissing-prototypes

# Compile with 'make DEBUG=true' to enable debugging
DEBUG = false
ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -g -DDEBUG
endif

all: inotail
inotail: inotail.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

install: inotail
	install -m 775 -D inotail $(BINDIR)/inotail
	install -m 644 -D inotail.1 $(MANDIR)/inotail.1
	gzip -9 $(MANDIR)/inotail.1

uninstall:
	rm $(BINDIR)/inotail $(MANDIR)/inotail.1*

cscope:
	cscope -b

release:
	git-archive --format=tar --prefix=inotail-$(VERSION)/ HEAD | gzip -9v > ../inotail-$(VERSION).tar.gz
	git-archive --format=tar --prefix=inotail-$(VERSION)/ HEAD | bzip2 -9v > ../inotail-$(VERSION).tar.bz2

clean:
	rm -f inotail *.o cscope.*
