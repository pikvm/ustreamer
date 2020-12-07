-include config.mk

PROG ?= ustreamer
DESTDIR ?=
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=

RPI_VC_HEADERS ?= /opt/vc/include
RPI_VC_LIBS ?= /opt/vc/lib

BUILD ?= build

LINTERS_IMAGE ?= $(PROG)-linters


# =====
_PROG_LIBS = -lm -ljpeg -pthread -levent -levent_pthreads -luuid
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE
_PROG_SRCS = $(shell ls src/common/*.c src/ustreamer/*.c src/ustreamer/http/*.c src/ustreamer/encoders/cpu/*.c src/ustreamer/encoders/hw/*.c)


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_RAWSINK)),)
_PROG_LIBS += -lrt
override CFLAGS += -DWITH_RAWSINK
_PROG_SRCS += $(shell ls src/rawsink/*.c)
endif


ifneq ($(call optbool,$(WITH_OMX)),)
_PROG_LIBS += -lbcm_host -lvcos -lopenmaxil -L$(RPI_VC_LIBS)
override CFLAGS += -DWITH_OMX -DOMX_SKIP64BIT -I$(RPI_VC_HEADERS)
_PROG_SRCS += $(shell ls src/ustreamer/encoders/omx/*.c)
endif


ifneq ($(call optbool,$(WITH_GPIO)),)
_PROG_LIBS += -lgpiod
override CFLAGS += -DWITH_GPIO
_PROG_SRCS += $(shell ls src/ustreamer/gpio/*.c)
endif


WITH_PTHREAD_NP ?= 1
ifneq ($(call optbool,$(WITH_PTHREAD_NP)),)
override CFLAGS += -DWITH_PTHREAD_NP
endif


WITH_SETPROCTITLE ?= 1
ifneq ($(call optbool,$(WITH_SETPROCTITLE)),)
ifeq ($(shell uname -s | tr A-Z a-z),linux)
_PROG_LIBS += -lbsd
endif
override CFLAGS += -DWITH_SETPROCTITLE
endif


# =====
all: $(PROG)


install: $(PROG)
	install -Dm755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	install -Dm644 $(PROG).1 $(DESTDIR)$(MANPREFIX)/man1/$(PROG).1
	gzip $(DESTDIR)$(MANPREFIX)/man1/$(PROG).1


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(PROG)


uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/$(PROG)


regen:
	tools/make-jpeg-h.py src/ustreamer/http/data/blank.jpeg src/ustreamer/http/data/blank_jpeg.h BLANK
	tools/make-html-h.py src/ustreamer/http/data/index.html src/ustreamer/http/data/index_html.h INDEX


$(PROG): $(_PROG_SRCS:%.c=$(BUILD)/%.o)
	$(info -- LD $@)
	@ $(CC) $^ -o $@ $(LDFLAGS) $(_PROG_LIBS)
	$(info ===== Build complete =====)
	$(info == CC      = $(CC))
	$(info == LIBS    = $(_PROG_LIBS))
	$(info == CFLAGS  = $(CFLAGS))
	$(info == LDFLAGS = $(LDFLAGS))


$(BUILD)/%.o: %.c
	$(info -- CC $<)
	@ mkdir -p $(dir $@) || true
	@ $(CC) $< -o $@ $(CFLAGS)


release:
	make clean
	make tox
	make push
	make bump V=$(V)
	make push
	make clean


tox: linters
	time docker run --rm \
			--volume `pwd`:/src:ro \
			--volume `pwd`/linters:/src/linters:rw \
		-t $(LINTERS_IMAGE) bash -c " \
			cd /src \
			&& tox -q -c linters/tox.ini $(if $(E),-e $(E),-p auto) \
		"


linters:
	docker build \
			$(if $(call optbool,$(NC)),--no-cache,) \
			--rm \
			--tag $(LINTERS_IMAGE) \
		-f linters/Dockerfile linters


bump:
	bumpversion $(if $(V),$(V),minor)


push:
	git push
	git push --tags


clean-all: linters clean
	- docker run --rm \
			--volume `pwd`:/src \
		-it $(LINTERS_IMAGE) bash -c "cd src && rm -rf linters/{.tox,.mypy_cache}"
clean:
	rm -rf pkg/arch/pkg pkg/arch/src pkg/arch/v*.tar.gz pkg/arch/ustreamer-*.pkg.tar.{xz,zst}
	rm -rf $(PROG) $(BUILD) vgcore.* *.sock

.PHONY: linters
