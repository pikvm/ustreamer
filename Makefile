DESTDIR ?=
PREFIX ?= /usr/local
CFLAGS ?= -O3
LDFLAGS ?=

CC = gcc
LIBS = -lm -ljpeg -pthread -levent -levent_pthreads
override CFLAGS += -c -std=c99 -Wall -Wextra -D_GNU_SOURCE
SOURCES = $(shell ls src/*.c src/jpeg/*.c)
OBJECTS = $(SOURCES:.c=.o)
PROG = ustreamer


ifeq ($(shell ls -d /opt/vc/include 2>/dev/null), /opt/vc/include)
SOURCES += $(shell ls src/omx/*.c)
LIBS += -lbcm_host -lvcos -lopenmaxil -L/opt/vc/lib
override CFLAGS += -DOMX_ENCODER -DOMX_SKIP64BIT -I/opt/vc/include
endif


all: $(SOURCES) $(PROG)


install: $(PROG)
	install -Dm755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)


regen:
	tools/make-jpg-h.py src/data/blank.jpg src/data/blank.h BLANK 640 480


$(PROG): $(OBJECTS)
	$(CC) $(LIBS) $(LDFLAGS) $(OBJECTS) -o $@


.c.o:
	$(CC) $(LIBS) $(CFLAGS) $< -o $@


release:
	make clean
	make push
	make bump
	make push
	make clean


bump:
	bumpversion minor


push:
	git push
	git push --tags


clean:
	rm -f src/*.o src/{jpeg,omx}/*.o vgcore.* $(PROG)
	rm -rf src/ustreamer-* src/v*.tar.gz v*.tar.gz
