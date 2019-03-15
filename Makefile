DESTDIR ?=
PREFIX ?= /usr/local
CFLAGS ?= -O3
LDFLAGS ?=
CC ?= gcc


# =====
LIBS = -lm -ljpeg -pthread -levent -levent_pthreads -luuid
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE
SOURCES = $(shell ls src/*.c src/encoders/cpu/*.c src/encoders/hw/*.c)
PROG = ustreamer

ifeq ($(shell ls -d /opt/vc/include 2>/dev/null), /opt/vc/include)
SOURCES += $(shell ls src/encoders/omx/*.c)
LIBS += -lbcm_host -lvcos -lopenmaxil -L/opt/vc/lib
override CFLAGS += -DWITH_OMX_ENCODER -DOMX_SKIP64BIT -I/opt/vc/include
endif

OBJECTS = $(SOURCES:.c=.o)


# =====
all: $(SOURCES) $(PROG)


install: $(PROG)
	install -Dm755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)


regen:
	tools/make-jpeg-h.py src/data/blank.jpeg src/data/blank_jpeg.h BLANK 640 480
	tools/make-html-h.py src/data/index.html src/data/index_html.h HTML_INDEX_PAGE


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

clean-all: clean
clean:
	rm -f src/*.o src/encoders/*/*.o vgcore.* *.sock $(PROG)
	rm -rf pkg src/$(PROG)-* src/v*.tar.gz v*.tar.gz $(PROG)-*.pkg.tar.xz
