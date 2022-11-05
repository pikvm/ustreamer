DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=


# =====
_USTR = ustreamer.bin
_DUMP = ustreamer-dump.bin

_CFLAGS = -MD -c -std=c17 -Wall -Wextra -D_GNU_SOURCE $(CFLAGS)
_LDFLAGS = $(LDFLAGS)

_COMMON_LIBS = -lm -ljpeg -pthread -lrt

_USTR_LIBS = $(_COMMON_LIBS) -levent -levent_pthreads
_USTR_SRCS = $(shell ls \
	libs/*.c \
	ustreamer/*.c \
	ustreamer/http/*.c \
	ustreamer/data/*.c \
	ustreamer/encoders/cpu/*.c \
	ustreamer/encoders/hw/*.c \
	ustreamer/*.c \
)

_DUMP_LIBS = $(_COMMON_LIBS)
_DUMP_SRCS = $(shell ls \
	libs/*.c \
	dump/*.c \
)

_BUILD = build


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_GPIO)),)
_USTR_LIBS += -lgpiod
override _CFLAGS += -DWITH_GPIO
_USTR_SRCS += $(shell ls ustreamer/gpio/*.c)
endif


ifneq ($(call optbool,$(WITH_SYSTEMD)),)
_USTR_LIBS += -lsystemd
override _CFLAGS += -DWITH_SYSTEMD
_USTR_SRCS += $(shell ls ustreamer/http/systemd/*.c)
endif


WITH_PTHREAD_NP ?= 1
ifneq ($(call optbool,$(WITH_PTHREAD_NP)),)
override _CFLAGS += -DWITH_PTHREAD_NP
endif


WITH_SETPROCTITLE ?= 1
ifneq ($(call optbool,$(WITH_SETPROCTITLE)),)
ifeq ($(shell uname -s | tr A-Z a-z),linux)
_USTR_LIBS += -lbsd
endif
override _CFLAGS += -DWITH_SETPROCTITLE
endif


# =====
all: $(_USTR) $(_DUMP)


install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m755 $(_USTR) $(DESTDIR)$(PREFIX)/bin/$(subst .bin,,$(_USTR))
	install -m755 $(_DUMP) $(DESTDIR)$(PREFIX)/bin/$(subst .bin,,$(_DUMP))


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(subst .bin,,$(_USTR))
	strip $(DESTDIR)$(PREFIX)/bin/$(subst .bin,,$(_DUMP))


$(_USTR): $(_USTR_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	@ $(CC) $^ -o $@ $(_LDFLAGS) $(_USTR_LIBS)


$(_DUMP): $(_DUMP_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	@ $(CC) $^ -o $@ $(_LDFLAGS) $(_DUMP_LIBS)


$(_BUILD)/%.o: %.c
	$(info -- CC $<)
	@ mkdir -p $(dir $@) || true
	@ $(CC) $< -o $@ $(_CFLAGS)


clean:
	rm -rf $(_USTR) $(_DUMP) $(_BUILD)


_OBJS = $(_USTR_SRCS:%.c=$(_BUILD)/%.o) $(_DUMP_SRCS:%.c=$(_BUILD)/%.o)
-include $(_OBJS:%.o=%.d)
