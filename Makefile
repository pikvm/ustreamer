PROG ?= ustreamer
DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=


# =====
LIBS = -lm -ljpeg -pthread -levent -levent_pthreads -luuid
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE
SOURCES = $(shell ls src/*.c src/http/*.c src/encoders/cpu/*.c src/encoders/hw/*.c)

ifeq ($(shell ls -d /opt/vc/include 2>/dev/null), /opt/vc/include)
SOURCES += $(shell ls src/encoders/omx/*.c)
LIBS += -lbcm_host -lvcos -lopenmaxil -L/opt/vc/lib
override CFLAGS += -DWITH_OMX_ENCODER -DOMX_SKIP64BIT -I/opt/vc/include
endif


# =====
all: $(SOURCES) $(PROG)


install: $(PROG)
	install -Dm755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(PROG)


uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/$(PROG)


regen:
	tools/make-jpeg-h.py src/http/data/blank.jpeg src/http/data/blank_jpeg.h BLANK
	tools/make-html-h.py src/http/data/index.html src/http/data/index_html.h INDEX


$(PROG): $(SOURCES:.c=.o)
	$(CC) $(SOURCES:.c=.o) -o $@ $(LDFLAGS) $(LIBS)


.c.o:
	$(CC) $< -o $@ $(CFLAGS) $(LIBS)


release:
	make clean
	make push
	make bump
	make push
	make clean


bump:
	bumpversion $(if $(V), $(V), minor)


push:
	git push
	git push --tags


clean-all: clean
clean:
	find src -name '*.o' -exec rm '{}' \;
	rm -rf pkg/arch/pkg pkg/arch/src pkg/arch/v*.tar.gz pkg/arch/ustreamer-*.pkg.tar.xz
	rm -f vgcore.* *.sock $(PROG)
