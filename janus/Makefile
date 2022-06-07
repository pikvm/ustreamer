DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=


# =====
_PLUGIN = libjanus_ustreamer.so

_CFLAGS = -fPIC -MD -c -std=c17 -Wall -Wextra -D_GNU_SOURCE $(shell pkg-config --cflags glib-2.0) $(CFLAGS)
_LDFLAGS = -shared -lm -pthread -lrt -ljansson -lopus -lasound -lspeexdsp $(shell pkg-config --libs glib-2.0) $(LDFLAGS)

_SRCS = $(shell ls src/uslibs/*.c src/*.c)

_BUILD = build


define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


WITH_PTHREAD_NP ?= 1
ifneq ($(call optbool,$(WITH_PTHREAD_NP)),)
override _CFLAGS += -DWITH_PTHREAD_NP
endif


# =====
$(_PLUGIN): $(_SRCS:%.c=$(_BUILD)/%.o)
	$(info == SO $@)
	@ $(CC) $^ -o $@ $(_LDFLAGS)


$(_BUILD)/%.o: %.c
	$(info -- CC $<)
	@ mkdir -p $(dir $@) || true
	@ $(CC) $< -o $@ $(_CFLAGS)



install: $(_PLUGIN)
	mkdir -p $(DESTDIR)$(PREFIX)/lib/ustreamer/janus
	install -m755 $(_PLUGIN) $(DESTDIR)$(PREFIX)/lib/ustreamer/janus/$(PLUGIN)


clean:
	rm -rf $(_PLUGIN) $(_BUILD)


_OBJS = $(_SRCS:%.c=$(_BUILD)/%.o)
-include $(_OBJS:%.o=%.d)
