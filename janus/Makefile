R_DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O3
LDFLAGS ?=


# =====
_PLUGIN = libjanus_ustreamer.so

_CFLAGS = -fPIC -MD -c -std=c17 -Wall -Wextra -D_GNU_SOURCE $(shell $(PKG_CONFIG) --cflags glib-2.0) $(CFLAGS)
_LDFLAGS = -shared -lm -pthread -lrt -ljansson -lopus -lasound -lspeexdsp $(shell $(PKG_CONFIG) --libs glib-2.0) $(LDFLAGS)

_SRCS = $(shell ls src/uslibs/*.c src/*.c)

_BUILD = build


ifneq ($(MK_WITH_PTHREAD_NP),)
override _CFLAGS += -DWITH_PTHREAD_NP
endif


# =====
$(_PLUGIN): $(_SRCS:%.c=$(_BUILD)/%.o)
	$(info == SO $@)
	$(ECHO) $(CC) $^ -o $@ $(_LDFLAGS)


$(_BUILD)/%.o: %.c
	$(info -- CC $<)
	$(ECHO) mkdir -p $(dir $@) || true
	$(ECHO) $(CC) $< -o $@ $(_CFLAGS)



install: $(_PLUGIN)
	mkdir -p $(R_DESTDIR)$(PREFIX)/lib/ustreamer/janus
	install -m755 $(_PLUGIN) $(R_DESTDIR)$(PREFIX)/lib/ustreamer/janus/$(PLUGIN)


clean:
	rm -rf $(_PLUGIN) $(_BUILD)


_OBJS = $(_SRCS:%.c=$(_BUILD)/%.o)
-include $(_OBJS:%.o=%.d)
