R_DESTDIR ?=
PREFIX ?= /usr/local

PY ?= python3


# =====
all: root
root: $(shell find src -type f,l) setup.py
	$(info == PY_BUILD ustreamer-*.so)
	rm -rf root
	$(ECHO) $(PY) -m build --skip-dependency-check --no-isolation
	$(ECHO) $(PY) -m pip install dist/*.whl --ignore-installed --root=./root


install:
	$(PY) -m pip install dist/*.whl --ignore-installed --prefix=$(PREFIX) --root=$(if $(R_DESTDIR),$(R_DESTDIR),/)


clean:
	rm -rf root dist ustreamer.egg-info
