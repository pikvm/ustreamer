-include ../config.mk

DESTDIR ?=
PREFIX ?= /usr/local

PY ?= python3


# =====
all:
	$(info == PY_BUILD ustreamer-*.so)
	@ $(PY) setup.py build


install:
	$(PY) setup.py install --prefix=$(PREFIX) --root=$(if $(DESTDIR),$(DESTDIR),/)


clean:
	rm -rf build
