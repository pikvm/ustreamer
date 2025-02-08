R_DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O3
LDFLAGS ?=


# =====
_USTR = ustreamer.bin
_DUMP = ustreamer-dump.bin
_V4P = ustreamer-v4p.bin

_CFLAGS = -MD -c -std=c17 -Wall -Wextra -D_GNU_SOURCE $(CFLAGS)

_USTR_LDFLAGS = $(LDFLAGS) -lm -ljpeg -pthread -lrt -latomic -levent -levent_pthreads
_DUMP_LDFLAGS = $(LDFLAGS) -lm -ljpeg -pthread -lrt -latomic
_V4P_LDFLAGS = $(LDFLAGS) -lm -ljpeg -pthread -lrt -latomic

_USTR_SRCS = $(shell ls \
	libs/*.c \
	ustreamer/*.c \
	ustreamer/http/*.c \
	ustreamer/data/*.c \
	ustreamer/encoders/cpu/*.c \
	ustreamer/encoders/hw/*.c \
	ustreamer/*.c \
)

_DUMP_SRCS = $(shell ls \
	libs/*.c \
	dump/*.c \
)

_V4P_SRCS = $(shell ls \
	libs/*.c \
	libs/drm/*.c \
	v4p/*.c \
)

_BUILD = build

_TARGETS = $(_USTR) $(_DUMP)
_OBJS = $(_USTR_SRCS:%.c=$(_BUILD)/%.o) $(_DUMP_SRCS:%.c=$(_BUILD)/%.o)


# =====
ifneq ($(MK_WITH_PYTHON),)
override _CFLAGS += -DMK_WITH_PYTHON
endif

ifneq ($(MK_WITH_JANUS),)
override _CFLAGS += -DMK_WITH_JANUS
endif

ifneq ($(MK_WITH_GPIO),)
override _CFLAGS += -DMK_WITH_GPIO -DWITH_GPIO $(shell $(PKG_CONFIG) --atleast-version=2 libgpiod 2> /dev/null && echo -DHAVE_GPIOD2)
override _USTR_LDFLAGS += -lgpiod
override _USTR_SRCS += $(shell ls ustreamer/gpio/*.c)
endif

ifneq ($(MK_WITH_SYSTEMD),)
override _CFLAGS += -DMK_WITH_SYSTEMD -DWITH_SYSTEMD
override _USTR_LDFLAGS += -lsystemd
override _USTR_SRCS += $(shell ls ustreamer/http/systemd/*.c)
endif

ifneq ($(MK_WITH_PTHREAD_NP),)
override _CFLAGS += -DMK_WITH_PTHREAD_NP -DWITH_PTHREAD_NP
endif

ifneq ($(MK_WITH_SETPROCTITLE),)
override _CFLAGS += -DMK_WITH_SETPROCTITLE -DWITH_SETPROCTITLE
ifeq ($(shell uname -s | tr A-Z a-z),linux)
override _USTR_LDFLAGS += -lbsd
endif
endif

ifneq ($(MK_WITH_PDEATHSIG),)
override _CFLAGS += -DMK_WITH_PDEATHSIG -DWITH_PDEATHSIG
endif

ifneq ($(MK_WITH_V4P),)
override _TARGETS += $(_V4P)
override _OBJS += $(_V4P_SRCS:%.c=$(_BUILD)/%.o)
override _CFLAGS += -DMK_WITH_V4P -DWITH_V4P $(shell $(PKG_CONFIG) --cflags libdrm)
override _V4P_LDFLAGS += $(shell $(PKG_CONFIG) --libs libdrm)
override _USTR_SRCS += $(shell ls libs/drm/*.c)
override _USTR_LDFLAGS += $(shell $(PKG_CONFIG) --libs libdrm)
endif


# =====
all: $(_TARGETS)


install: all
	mkdir -p $(R_DESTDIR)$(PREFIX)/bin
	for i in $(subst .bin,,$(_TARGETS)); do \
		install -m755 $$i.bin $(R_DESTDIR)$(PREFIX)/bin/$$i; \
	done


install-strip: install
	for i in $(subst .bin,,$(_TARGETS)); do \
		strip $(R_DESTDIR)$(PREFIX)/bin/$$i; \
	done


$(_USTR): $(_USTR_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	$(ECHO) $(CC) $^ -o $@ $(_USTR_LDFLAGS)


$(_DUMP): $(_DUMP_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	$(ECHO) $(CC) $^ -o $@ $(_DUMP_LDFLAGS)


$(_V4P): $(_V4P_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	$(ECHO) $(CC) $^ -o $@ $(_V4P_LDFLAGS)


$(_BUILD)/%.o: %.c
	$(info -- CC $<)
	$(ECHO) mkdir -p $(dir $@) || true
	$(ECHO) $(CC) $< -o $@ $(_CFLAGS)


clean:
	rm -rf $(_USTR) $(_DUMP) $(_V4P) $(_BUILD)


-include $(_OBJS:%.o=%.d)
