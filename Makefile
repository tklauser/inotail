# Makefile for inotail
#
# Copyright (C) 2006, 2007 Tobias Klauser <tklauser@distanz.ch>
#
# Licensed under the terms of the GNU General Public License; version 2 or later.

VERSION	= 0.2

# Paths
prefix	= /usr/local
DESTDIR	=

CC	:= gcc
CFLAGS	:= $(CFLAGS) -Wall -pipe -D_USE_SOURCE -DVERSION="\"$(VERSION)\""
WARN	:= -Wstrict-prototypes -Wsign-compare -Wshadow \
	   -Wchar-subscripts -Wmissing-declarations -Wnested-externs \
	   -Wpointer-arith -Wcast-align -Wmissing-prototypes
CFLAGS	+= $(WARN)
LDFLAGS	:=

# Compile with 'make DEBUG=true' to enable debugging
DEBUG = false
ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -g -DDEBUG -fmudflap
	LDFLAGS += -lmudflap
endif

all: inotail
inotail: inotail.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

install: inotail
	install -m 775 -D inotail $(DESTDIR)$(prefix)/bin/inotail
	install -m 644 -D inotail.1 $(DESTDIR)$(prefix)/share/man/man1/inotail.1

cscope:
	cscope -b

release:
	git-archive --format=tar --prefix=inotail-0.2/ HEAD | gzip -9v > ../inotail-$(VERSION).tar.gz
	git-archive --format=tar --prefix=inotail-0.2/ HEAD | bzip2 -9v > ../inotail-$(VERSION).tar.bz2

clean:
	rm -f inotail *.o cscope.*
