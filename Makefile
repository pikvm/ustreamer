DESTDIR ?=
PREFIX ?= /usr/local

LIBS = -lm -ljpeg -pthread -levent -levent_pthreads
CC = gcc
CFLAGS = -c -O3 -Wall -Wextra
LDFLAGS =
SOURCES = $(shell ls src/*.c)
OBJECTS = $(SOURCES:.c=.o)
PROG = ustreamer


all: $(SOURCES) $(PROG)


install: $(PROG)
	install -Dm755 ustreamer $(DESTDIR)$(PREFIX)/bin/ustreamer


regen:
	tools/make-jpg-h.py src/data/blank.jpg src/data/blank.h BLANK 640 480


$(PROG): $(OBJECTS)
	$(CC) $(LIBS) $(LDFLAGS) $(OBJECTS) -o $@


.c.o:
	$(CC) $(LIBS) $(CFLAGS) $< -o $@


clean:
	rm -f src/*.o vgcore.* $(PROG)
