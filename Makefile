CC := gcc
CFLAGS := -Wall -D_USE_SOURCE

DEBUG = false

ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -g -DDEBUG
endif

PROGRAMS := inotail inotify-watchdir simpletail

all: $(PROGRAMS)

inotail: inotail.o

inotify-watchdir: inotify-watchdir.o

simpletail: simpletail.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o
	rm -f $(PROGRAMS)
