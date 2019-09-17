-include config.mk

PROG ?= ustreamer
DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=

RPI_VC_HEADERS ?= /opt/vc/include
RPI_VC_LIBS ?= /opt/vc/lib

BUILD ?= build


# =====
_LIBS = -lm -ljpeg -pthread -levent -levent_pthreads -luuid
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE
_SRCS = $(shell ls src/*.c src/http/*.c src/encoders/cpu/*.c src/encoders/hw/*.c)


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_OMX)),)
_LIBS += -lbcm_host -lvcos -lopenmaxil -L$(RPI_VC_LIBS)
override CFLAGS += -DWITH_OMX -DOMX_SKIP64BIT -I$(RPI_VC_HEADERS)
_SRCS += $(shell ls src/encoders/omx/*.c)
endif


ifneq ($(call optbool,$(WITH_GPIO)),)
_LIBS += -lwiringPi
override CFLAGS += -DWITH_GPIO
endif


WITH_PTHREAD_NP ?= 1
ifneq ($(call optbool,$(WITH_PTHREAD_NP)),)
override CFLAGS += -DWITH_PTHREAD_NP
endif


# =====
all: $(PROG)


install: $(PROG)
	install -Dm755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(PROG)


uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/$(PROG)


regen:
	tools/make-jpeg-h.py src/http/data/blank.jpeg src/http/data/blank_jpeg.h BLANK
	tools/make-html-h.py src/http/data/index.html src/http/data/index_html.h INDEX


$(PROG): $(_SRCS:%.c=$(BUILD)/%.o)
	$(info -- LD $@)
	@ $(CC) $^ -o $@ $(LDFLAGS) $(_LIBS)
	$(info ===== Build complete =====)
	$(info == CC      = $(CC))
	$(info == LIBS    = $(_LIBS))
	$(info == CFLAGS  = $(CFLAGS))
	$(info == LDFLAGS = $(LDFLAGS))


$(BUILD)/%.o: %.c
	$(info -- CC $<)
	@ mkdir -p $(dir $@) || true
	@ $(CC) $< -o $@ $(CFLAGS) $(_LIBS)


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
	rm -rf pkg/arch/pkg pkg/arch/src pkg/arch/v*.tar.gz pkg/arch/ustreamer-*.pkg.tar.xz
	rm -rf $(PROG) $(BUILD) vgcore.* *.sock
