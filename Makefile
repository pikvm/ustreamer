-include config.mk

USTR ?= ustreamer
REC ?= ustreamer-recorder
DESTDIR ?=
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=

RPI_VC_HEADERS ?= /opt/vc/include
RPI_VC_LIBS ?= /opt/vc/lib

BUILD ?= build

LINTERS_IMAGE ?= $(USTR)-linters


# =====
override CFLAGS += -c -std=c11 -Wall -Wextra -D_GNU_SOURCE

_COMMON_LIBS = -lm -ljpeg -pthread

_USTR_LIBS = $(_COMMON_LIBS) -levent -levent_pthreads -luuid
_USTR_SRCS = $(shell ls \
	src/libs/common/*.c \
	src/ustreamer/*.c \
	src/ustreamer/http/*.c \
	src/ustreamer/data/*.c \
	src/ustreamer/encoders/cpu/*.c \
	src/ustreamer/encoders/hw/*.c \
)

_REC_LIBS = $(_COMMON_LIBS) -lrt -lbcm_host -lvcos -lmmal -lmmal_core -lmmal_util -lmmal_vc_client -lmmal_components -L$(RPI_VC_LIBS)
_REC_SRCS = $(shell ls \
	src/libs/common/*.c \
	src/libs/memsink/*.c \
	src/libs/h264/*.c \
	src/recorder/*.c \
)


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_MEMSINK)),)
_USTR_LIBS += -lrt
override CFLAGS += -DWITH_MEMSINK
_USTR_SRCS += $(shell ls src/libs/memsink/*.c)
endif


ifneq ($(call optbool,$(WITH_OMX)),)
_USTR_LIBS += -lbcm_host -lvcos -lopenmaxil -L$(RPI_VC_LIBS)
override CFLAGS += -DWITH_OMX -DOMX_SKIP64BIT -I$(RPI_VC_HEADERS)
_USTR_SRCS += $(shell ls src/ustreamer/encoders/omx/*.c)
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


ifneq ($(call optbool,$(WITH_MEMSINK)),)
ifneq ($(call optbool,$(WITH_OMX)),)
_ENABLE_REC = 1
endif
endif


# =====
all: $(USTR) $(REC)


install: $(USTR) $(REC)
	install -Dm755 $(USTR) $(DESTDIR)$(PREFIX)/bin/$(USTR)
	install -Dm644 $(USTR).1 $(DESTDIR)$(MANPREFIX)/man1/$(USTR).1
	gzip $(DESTDIR)$(MANPREFIX)/man1/$(USTR).1
ifneq ($(_ENABLE_REC),)
	install -Dm755 $(DESTDIR)$(PREFIX)/bin/$(REC)
endif


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(USTR)
ifneq ($(_ENABLE_REC),)
	strip $(DESTDIR)$(PREFIX)/bin/$(REC)
endif


uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(USTR) \
		$(DESTDIR)$(PREFIX)/bin/$(REC) \
		$(DESTDIR)$(MANPREFIX)/man1/$(USTR).1


regen:
	tools/make-jpeg-h.py src/ustreamer/data/blank.jpeg src/ustreamer/data/blank_jpeg.c BLANK
	tools/make-html-h.py src/ustreamer/data/index.html src/ustreamer/data/index_html.c INDEX


$(USTR): $(_USTR_SRCS:%.c=$(BUILD)/%.o)
	$(info -- LD $@)
	@ $(CC) $^ -o $@ $(LDFLAGS) $(_USTR_LIBS)
	$(info == CC      = $(CC))
	$(info == LIBS    = $(_USTR_LIBS))
	$(info == CFLAGS  = $(CFLAGS))
	$(info == LDFLAGS = $(LDFLAGS))


ifneq ($(_ENABLE_REC),)
$(REC): $(_REC_SRCS:%.c=$(BUILD)/%.o)
	$(info -- LD $@)
	@ $(CC) $^ -o $@ $(LDFLAGS) $(_REC_LIBS)
	$(info == CC      = $(CC))
	$(info == LIBS    = $(_REC_LIBS))
	$(info == CFLAGS  = $(CFLAGS))
	$(info == LDFLAGS = $(LDFLAGS))
else
$(REC):
	@ true
endif


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
	rm -rf $(USTR) $(REC) $(BUILD) vgcore.* *.sock

.PHONY: linters
