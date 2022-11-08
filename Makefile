-include config.mk

DESTDIR ?=
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

CC ?= gcc
PY ?= python3
CFLAGS ?= -O3
LDFLAGS ?=

RPI_VC_HEADERS ?= /opt/vc/include
RPI_VC_LIBS ?= /opt/vc/lib

export

_LINTERS_IMAGE ?= ustreamer-linters


# =====
define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


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
	@ ln -sf src/ustreamer.bin ustreamer
	@ ln -sf src/ustreamer-dump.bin ustreamer-dump


python:
	$(MAKE) -C python
	@ ln -sf python/build/lib.*/*.so .


janus:
	$(MAKE) -C janus
	@ ln -sf janus/*.so .


install: all
	$(MAKE) -C src install
ifneq ($(call optbool,$(WITH_PYTHON)),)
	$(MAKE) -C python install
endif
ifneq ($(call optbool,$(WITH_JANUS)),)
	$(MAKE) -C janus install
endif
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	for man in $(shell ls man); do \
		install -m644 man/$$man $(DESTDIR)$(MANPREFIX)/man1/$$man; \
		gzip -f $(DESTDIR)$(MANPREFIX)/man1/$$man; \
	done


install-strip: install
	$(MAKE) -C src install-strip


regen:
	tools/$(MAKE)-jpeg-h.py src/ustreamer/data/blank.jpeg src/ustreamer/data/blank_jpeg.c BLANK
	tools/$(MAKE)-ico-h.py src/ustreamer/data/favicon.ico src/ustreamer/data/favicon_ico.c FAVICON
	tools/$(MAKE)-html-h.py src/ustreamer/data/index.html src/ustreamer/data/index_html.c INDEX


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
	rm -f ustreamer ustreamer-dump *.so
	$(MAKE) -C src clean
	$(MAKE) -C python clean
	$(MAKE) -C janus clean


.PHONY: python janus linters
