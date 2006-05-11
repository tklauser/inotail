# Paths
prefix	= $(HOME)
BINDIR	= ${prefix}/bin
DESTDIR	=

CC := gcc
CFLAGS := -Wall -D_USE_SOURCE

INSTALL := install

DEBUG = false

ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -g -DDEBUG
endif

PROGRAMS := inotail inotail-old inotify-watchdir #simpletail

all: $(PROGRAMS)

inotail: inotail.o

inotail-old: inotail-old.o

inotify-watchdir: inotify-watchdir.o

#simpletail: simpletail.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: inotail
	${INSTALL} -m 775 inotail ${DESTDIR}${BINDIR}

clean:
	rm -f *.o
	rm -f $(PROGRAMS)
