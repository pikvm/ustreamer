PROG ?= ustreamer
DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=

RPI_VC_HEADERS ?= /opt/vc/include
RPI_VC_LIBS ?= /opt/vc/lib


# =====
LIBS = -lm -ljpeg -pthread -levent -levent_pthreads -luuid
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE
SOURCES = $(shell ls src/*.c src/http/*.c src/encoders/cpu/*.c src/encoders/hw/*.c)

ifeq ($(WITH_OMX_ENCODER),)
else
	LIBS += -lbcm_host -lvcos -lopenmaxil -L$(RPI_VC_LIBS)
	override CFLAGS += -DWITH_OMX_ENCODER -DOMX_SKIP64BIT -I$(RPI_VC_HEADERS)
	SOURCES += $(shell ls src/encoders/omx/*.c)
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
	$(info -- LINKING $@)
	@ $(CC) $(SOURCES:.c=.o) -o $@ $(LDFLAGS) $(LIBS)
	$(info ===== Build complete =====)
	$(info == CC      = $(CC))
	$(info == LIBS    = $(LIBS))
	$(info == CFLAGS  = $(CFLAGS))
	$(info == LDFLAGS = $(LDFLAGS))


.c.o:
	$(info -- CC $<)
	@ $(CC) $< -o $@ $(CFLAGS) $(LIBS)


release:
	make clean
	make push
	make bump
	make push
	make clean


bump:
	bumpversion $(if $(V),$(V),minor)


push:
	git push
	git push --tags


clean-all: clean
clean:
	find src -name '*.o' -exec rm '{}' \;
	rm -rf pkg/arch/pkg pkg/arch/src pkg/arch/v*.tar.gz pkg/arch/ustreamer-*.pkg.tar.xz
	rm -f vgcore.* *.sock $(PROG)
