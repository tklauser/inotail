# Makefile for inotail

VERSION = 0.1

# Paths
prefix	= /usr
DESTDIR	=

CC := gcc
CFLAGS := -Wall -pipe -D_USE_SOURCE -DVERSION="\"$(VERSION)\""
WARN := -Wstrict-prototypes -Wsign-compare -Wshadow \
	-Wchar-subscripts -Wmissing-declarations -Wnested-externs \
	-Wpointer-arith -Wcast-align -Wsign-compare -Wmissing-prototypes
CFLAGS += $(WARN)

# Compile with 'make DEBUG=true' to enable debugging
DEBUG = false
ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -g -DDEBUG
endif

all: Makefile inotail
inotail: inotail.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

install: inotail
	install -m 775 -D inotail $(DESTDIR)$(prefix)/bin/inotail
	install -m 644 -D inotail.1 $(DESTDIR)$(prefix)/share/man/man1/inotail.1

cscope:
	cscope -b

release:
	git-tar-tree HEAD inotail-$(VERSION) | gzip -9v > ../inotail-$(VERSION).tar.gz
	git-tar-tree HEAD inotail-$(VERSION) | bzip2 -9v > ../inotail-$(VERSION).tar.bz2

clean:
	rm -f inotail
	rm -f *.o
	rm -f cscope.out
