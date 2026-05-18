R_DESTDIR ?=
PREFIX ?= /usr/local

PY ?= python3


# =====
all: root
root: $(shell find src -type f,l) setup.py
	$(info == PY_BUILD ustreamer-*.so)
	rm -rf root
	$(ECHO) $(PY) -m build --wheel --skip-dependency-check --no-isolation


install:
	$(PY) -m installer --prefix=$(PREFIX) $(if $(R_DESTDIR),--destdir=$(R_DESTDIR),) dist/*.whl


clean:
	rm -rf root dist ustreamer.egg-info
