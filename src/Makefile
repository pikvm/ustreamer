DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=


# =====
_USTR = ustreamer.bin
_DUMP = ustreamer-dump.bin
_V4P = ustreamer-v4p.bin

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

_V4P_LIBS = $(_COMMON_LIBS)
_V4P_SRCS = $(shell ls \
	libs/*.c \
	v4p/*.c \
)

_BUILD = build


_TARGETS = $(_USTR) $(_DUMP)
_OBJS = $(_USTR_SRCS:%.c=$(_BUILD)/%.o) $(_DUMP_SRCS:%.c=$(_BUILD)/%.o)


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_GPIO)),)
_USTR_LIBS += -lgpiod
override _CFLAGS += -DWITH_GPIO $(shell pkg-config --atleast-version=2 libgpiod 2> /dev/null && echo -DHAVE_GPIOD2)
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


WITH_V4P ?= 0
ifneq ($(call optbool,$(WITH_V4P)),)
override _TARGETS += $(_V4P)
override _OBJS += $(_V4P_SRCS:%.c=$(_BUILD)/%.o)
override _CFLAGS += $(shell pkg-config --cflags libdrm)
_V4P_LDFLAGS = $(shell pkg-config --libs libdrm)
endif

# =====
all: $(_TARGETS)


install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	for i in $(_TARGETS); do install -m755 $$i $(DESTDIR)$(PREFIX)/bin/$(subst .bin,,$$i); done


install-strip: install
	for i in $(_TARGETS); do strip $(DESTDIR)$(PREFIX)/bin/$(subst .bin,,$$i); done


$(_USTR): $(_USTR_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	$(ECHO) $(CC) $^ -o $@ $(_LDFLAGS) $(_USTR_LIBS)


$(_DUMP): $(_DUMP_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	$(ECHO) $(CC) $^ -o $@ $(_LDFLAGS) $(_DUMP_LIBS)


$(_V4P): $(_V4P_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	$(ECHO) $(CC) $^ -o $@ $(_LDFLAGS) $(_V4P_LDFLAGS) $(_V4P_LIBS)


$(_BUILD)/%.o: %.c
	$(info -- CC $<)
	$(ECHO) mkdir -p $(dir $@) || true
	$(ECHO) $(CC) $< -o $@ $(_CFLAGS)


clean:
	rm -rf $(_USTR) $(_DUMP) $(_V4P) $(_BUILD)


-include $(_OBJS:%.o=%.d)
