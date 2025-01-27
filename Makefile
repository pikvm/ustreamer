-include config.mk

DESTDIR ?=
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

CC ?= gcc
PY ?= python3
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O3
LDFLAGS ?=

R_DESTDIR = $(if $(DESTDIR),$(shell realpath "$(DESTDIR)"),)

export

_LINTERS_IMAGE ?= ustreamer-linters


# =====
ifeq (__not_found__,$(shell which $(PKG_CONFIG) 2>/dev/null || echo "__not_found__"))
$(error "No $(PKG_CONFIG) found in $(PATH)")
endif


# =====
define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef

ifeq ($(V),)
	ECHO = @
endif

# =====
all:
	+ $(MAKE) apps
ifneq ($(call optbool,$(WITH_PYTHON)),)
	+ $(MAKE) python
endif
ifneq ($(call optbool,$(WITH_JANUS)),)
	+ $(MAKE) janus
endif


apps:
	$(MAKE) -C src
	for i in src/*.bin; do \
		test ! -x $$i || ln -sf $$i `basename $$i .bin`; \
	done


python:
	$(MAKE) -C python
	$(ECHO) ln -sf python/root/usr/lib/python*/site-packages/*.so .


janus:
	$(MAKE) -C janus
	$(ECHO) ln -sf janus/*.so .


install: all
	$(MAKE) -C src install
ifneq ($(call optbool,$(WITH_PYTHON)),)
	$(MAKE) -C python install
endif
ifneq ($(call optbool,$(WITH_JANUS)),)
	$(MAKE) -C janus install
endif
	mkdir -p $(R_DESTDIR)$(MANPREFIX)/man1
	for man in $(shell ls man); do \
		install -m644 man/$$man $(R_DESTDIR)$(MANPREFIX)/man1/$$man; \
		gzip -f $(R_DESTDIR)$(MANPREFIX)/man1/$$man; \
	done


install-strip: install
	$(MAKE) -C src install-strip


regen:
	tools/make-ico-h.py src/ustreamer/data/favicon.ico src/ustreamer/data/favicon_ico.c FAVICON
	tools/make-html-h.py src/ustreamer/data/index.html src/ustreamer/data/index_html.c INDEX


release:
	$(MAKE) clean
	$(MAKE) tox
	$(MAKE) push
	$(MAKE) bump V=$(V)
	$(MAKE) push
	$(MAKE) clean


tox: linters
	time docker run --rm \
			--volume `pwd`:/src:ro \
			--volume `pwd`/linters:/src/linters:rw \
		-t $(_LINTERS_IMAGE) bash -c " \
			cd /src \
			&& tox -q -c linters/tox.ini $(if $(E),-e $(E),-p auto) \
		"


linters:
	docker build \
			$(if $(call optbool,$(NC)),--no-cache,) \
			--rm \
			--tag $(_LINTERS_IMAGE) \
		-f linters/Dockerfile linters


bump:
	bumpversion $(if $(V),$(V),minor)


push:
	git push
	git push --tags


clean-all: linters clean
	- docker run --rm \
			--volume `pwd`:/src \
		-it $(_LINTERS_IMAGE) bash -c "cd src && rm -rf linters/{.tox,.mypy_cache}"


clean:
	rm -rf pkg/arch/pkg pkg/arch/src pkg/arch/v*.tar.gz pkg/arch/ustreamer-*.pkg.tar.{xz,zst}
	rm -f ustreamer ustreamer-* *.so
	$(MAKE) -C src clean
	$(MAKE) -C python clean
	$(MAKE) -C janus clean


.PHONY: python janus linters
