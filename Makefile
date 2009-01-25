# Makefile for inotail
#
# Copyright (C) 2006-2009 Tobias Klauser <tklauser@distanz.ch>
#
# Licensed under the terms of the GNU General Public License; version 2 or later.

P = inotail
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

all: $(P)
$(P): $(P).o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

install: $(P)
	install -m 775 -D $(P) $(BINDIR)/$(P)
	install -m 644 -D $(P).1 $(MANDIR)/$(P).1
	gzip -9 $(MANDIR)/$(P).1

uninstall:
	rm $(BINDIR)/$(P) $(MANDIR)/$(P).1*

cscope:
	cscope -b

archive:
	git-archive --format=tar --prefix=$(P)-$(VERSION)/ HEAD | gzip -9v > ../$(P)-$(VERSION).tar.gz
	git-archive --format=tar --prefix=$(P)-$(VERSION)/ HEAD | bzip2 -9v > ../$(P)-$(VERSION).tar.bz2

checksum: archive
	(cd ..; \
		sha1sum $(P)-$(VERSION).tar.gz > $(P)-$(VERSION).tar.gz.sha1; \
		sha1sum $(P)-$(VERSION).tar.bz2 > $(P)-$(VERSION).tar.bz2.sha1)

signature: archive
	(cd ..; \
		gpg -a --detach-sign $(P)-$(VERSION).tar.gz; \
		gpg -a --detach-sign $(P)-$(VERSION).tar.bz2)

release: archive checksum signature

clean:
	rm -f $(P) *.o cscope.*
