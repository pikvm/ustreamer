-include config.mk

USTR ?= ustreamer
DUMP ?= ustreamer-dump
DESTDIR ?=
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

CC ?= gcc
CFLAGS ?= -O3 -MD
LDFLAGS ?=

RPI_VC_HEADERS ?= /opt/vc/include
RPI_VC_LIBS ?= /opt/vc/lib

BUILD ?= build

LINTERS_IMAGE ?= $(USTR)-linters


# =====
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE

_COMMON_LIBS = -lm -ljpeg -pthread -lrt

_USTR_LIBS = $(_COMMON_LIBS) -levent -levent_pthreads -luuid
_USTR_SRCS = $(shell ls \
	src/libs/*.c \
	src/ustreamer/*.c \
	src/ustreamer/http/*.c \
	src/ustreamer/data/*.c \
	src/ustreamer/encoders/cpu/*.c \
	src/ustreamer/encoders/hw/*.c \
)

_DUMP_LIBS = $(_COMMON_LIBS)
_DUMP_SRCS = $(shell ls \
	src/libs/*.c \
	src/dump/*.c \
)


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_OMX)),)
_USTR_LIBS += -lbcm_host -lvcos -lvcsm -lopenmaxil -lmmal -lmmal_core -lmmal_util -lmmal_vc_client -lmmal_components -L$(RPI_VC_LIBS)
override CFLAGS += -DWITH_OMX -DOMX_SKIP64BIT -I$(RPI_VC_HEADERS)
_USTR_SRCS += $(shell ls \
	src/ustreamer/encoders/omx/*.c \
	src/ustreamer/h264/*.c \
)
endif


ifneq ($(call optbool,$(WITH_GPIO)),)
_USTR_LIBS += -lgpiod
override CFLAGS += -DWITH_GPIO
_USTR_SRCS += $(shell ls src/ustreamer/gpio/*.c)
endif


WITH_PTHREAD_NP ?= 1
ifneq ($(call optbool,$(WITH_PTHREAD_NP)),)
override CFLAGS += -DWITH_PTHREAD_NP
endif


WITH_SETPROCTITLE ?= 1
ifneq ($(call optbool,$(WITH_SETPROCTITLE)),)
ifeq ($(shell uname -s | tr A-Z a-z),linux)
_USTR_LIBS += -lbsd
endif
override CFLAGS += -DWITH_SETPROCTITLE
endif


# =====
all: $(USTR) $(DUMP) python


install: $(USTR) $(DUMP)
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MANPREFIX)/man1
	install -m755 $(USTR) $(DESTDIR)$(PREFIX)/bin/$(USTR)
	install -m755 $(DUMP) $(DESTDIR)$(PREFIX)/bin/$(DUMP)
	install -m644 man/$(USTR).1 $(DESTDIR)$(MANPREFIX)/man1/$(USTR).1
	install -m644 man/$(DUMP).1 $(DESTDIR)$(MANPREFIX)/man1/$(DUMP).1
	gzip -f $(DESTDIR)$(MANPREFIX)/man1/$(USTR).1
	gzip -f $(DESTDIR)$(MANPREFIX)/man1/$(DUMP).1
ifneq ($(call optbool,$(WITH_PYTHON)),)
	cd src/python && python3 setup.py install --prefix=$(PREFIX) --root=$(if $(DESTDIR),$(DESTDIR),/)
endif


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(USTR)
	strip $(DESTDIR)$(PREFIX)/bin/$(DUMP)


regen:
	tools/make-jpeg-h.py src/ustreamer/data/blank.jpeg src/ustreamer/data/blank_jpeg.c BLANK
	tools/make-html-h.py src/ustreamer/data/index.html src/ustreamer/data/index_html.c INDEX


$(USTR): $(_USTR_SRCS:%.c=$(BUILD)/%.o)
#	$(info ========================================)
	$(info == LD $@)
	@ $(CC) $^ -o $@ $(LDFLAGS) $(_USTR_LIBS)
#	$(info :: CC      = $(CC))
#	$(info :: LIBS    = $(_USTR_LIBS))
#	$(info :: CFLAGS  = $(CFLAGS))
#	$(info :: LDFLAGS = $(LDFLAGS))


$(DUMP): $(_DUMP_SRCS:%.c=$(BUILD)/%.o)
#	$(info ========================================)
	$(info == LD $@)
	@ $(CC) $^ -o $@ $(LDFLAGS) $(_DUMP_LIBS)
#	$(info :: CC      = $(CC))
#	$(info :: LIBS    = $(_DUMP_LIBS))
#	$(info :: CFLAGS  = $(CFLAGS))
#	$(info :: LDFLAGS = $(LDFLAGS))


$(BUILD)/%.o: %.c
	$(info -- CC $<)
	@ mkdir -p $(dir $@) || true
	@ $(CC) $< -o $@ $(CFLAGS)


python:
ifneq ($(call optbool,$(WITH_PYTHON)),)
	cd src/python && python3 setup.py build
	ln -sf src/python/build/lib.*/*.so .
else
	@ true
endif

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
	rm -rf $(USTR) $(DUMP) $(BUILD) src/python/build vgcore.* *.sock *.so


.PHONY: python linters


_OBJS = $(_USTR_SRCS:%.c=$(BUILD)/%.o) $(_DUMP_SRCS:%.c=$(BUILD)/%.o)
-include $(_OBJS:%.o=%.d)
