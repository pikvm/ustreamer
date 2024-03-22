-include ../config.mk

R_DESTDIR ?=
PREFIX ?= /usr/local

PY ?= python3


# =====
all:
	$(info == PY_BUILD ustreamer-*.so)
	$(ECHO) $(PY) setup.py build


install:
	$(PY) setup.py install --prefix=$(PREFIX) --root=$(if $(R_DESTDIR),$(R_DESTDIR),/)


clean:
	rm -rf build ustreamer.egg-info
