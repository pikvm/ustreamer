#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <Python.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"
#include "uslibs/frame.h"
#include "uslibs/memsinksh.h"


typedef struct {
	PyObject_HEAD

	char	*obj;
	double	lock_timeout;
	double	wait_timeout;
	double	drop_same_frames;

	int					fd;
	us_memsink_shared_s	*mem;

	u64				frame_id;
	ldf				frame_ts;
	us_frame_s		*frame;
} _MemsinkObject;


static void _MemsinkObject_destroy_internals(_MemsinkObject *self) {
	US_DELETE(self->mem, us_memsink_shared_unmap);
	US_CLOSE_FD(self->fd);
	US_DELETE(self->frame, us_frame_destroy);
}

static int _MemsinkObject_init(_MemsinkObject *self, PyObject *args, PyObject *kwargs) {
	self->lock_timeout = 1;
	self->wait_timeout = 1;

	static char *kws[] = {"obj", "lock_timeout", "wait_timeout", "drop_same_frames", NULL};
	if (!PyArg_ParseTupleAndKeywords(
		args, kwargs, "s|ddd", kws,
		&self->obj, &self->lock_timeout, &self->wait_timeout, &self->drop_same_frames)) {
		return -1;
	}

#	define SET_DOUBLE(x_field, x_cond) { \
			if (!(self->x_field x_cond)) { \
				PyErr_SetString(PyExc_ValueError, #x_field " must be " #x_cond); \
				return -1; \
			} \
		}
	SET_DOUBLE(lock_timeout, > 0);
	SET_DOUBLE(wait_timeout, > 0);
	SET_DOUBLE(drop_same_frames, >= 0);
#	undef SET_DOUBLE

	self->frame = us_frame_init();

	if ((self->fd = shm_open(self->obj, O_RDWR, 0)) == -1) {
		PyErr_SetFromErrno(PyExc_OSError);
		goto error;
	}
	if ((self->mem = us_memsink_shared_map(self->fd)) == NULL) {
		PyErr_SetFromErrno(PyExc_OSError);
		goto error;
	}
	return 0;

error:
	_MemsinkObject_destroy_internals(self);
	return -1;
}

static PyObject *_MemsinkObject_repr(_MemsinkObject *self) {
	char repr[1024];
	US_SNPRINTF(repr, 1023, "<Memsink(%s)>", self->obj);
	return Py_BuildValue("s", repr);
}

static void _MemsinkObject_dealloc(_MemsinkObject *self) {
	_MemsinkObject_destroy_internals(self);
	PyObject_Del(self);
}

static PyObject *_MemsinkObject_close(_MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	_MemsinkObject_destroy_internals(self);
	Py_RETURN_NONE;
}

static PyObject *_MemsinkObject_enter(_MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	Py_INCREF(self);
	return (PyObject *)self;
}

static PyObject *_MemsinkObject_exit(_MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	return PyObject_CallMethod((PyObject *)self, "close", "");
}

static int _wait_frame(_MemsinkObject *self) {
	const ldf deadline_ts = us_get_now_monotonic() + self->wait_timeout;

#	define RETURN_OS_ERROR { \
			Py_BLOCK_THREADS \
			PyErr_SetFromErrno(PyExc_OSError); \
			return -1; \
		}

	ldf now_ts;
	do {
		Py_BEGIN_ALLOW_THREADS

		const int retval = us_flock_timedwait_monotonic(self->fd, self->lock_timeout);
		now_ts = us_get_now_monotonic();

		if (retval < 0 && errno != EWOULDBLOCK) {
			RETURN_OS_ERROR;

		} else if (retval == 0) {
			us_memsink_shared_s *mem = self->mem;
			if (mem->magic == US_MEMSINK_MAGIC && mem->version == US_MEMSINK_VERSION && mem->id != self->frame_id) {
				if (self->drop_same_frames > 0) {
					if (
						US_FRAME_COMPARE_GEOMETRY(self->mem, self->frame)
						&& (self->frame_ts + self->drop_same_frames > now_ts)
						&& !memcmp(self->frame->data, mem->data, mem->used)
					) {
						self->frame_id = mem->id;
						goto drop;
					}
				}

				Py_BLOCK_THREADS
				return 0;
			}

			if (flock(self->fd, LOCK_UN) < 0) {
				RETURN_OS_ERROR;
			}
		}

	drop:
		if (usleep(1000) < 0) {
			RETURN_OS_ERROR;
		}
		Py_END_ALLOW_THREADS
		if (PyErr_CheckSignals() < 0) {
			return -1;
		}
	} while (now_ts < deadline_ts);

	return -2;
#	undef RETURN_OS_ERROR
}

static PyObject *_MemsinkObject_wait_frame(_MemsinkObject *self, PyObject *args, PyObject *kwargs) {
	if (self->mem == NULL || self->fd <= 0) {
		PyErr_SetString(PyExc_RuntimeError, "Closed");
		return NULL;
	}

	bool key_required = false;
	static char *kws[] = {"key_required", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", kws, &key_required)) {
		return NULL;
	}

	switch (_wait_frame(self)) {
		case 0: break;
		case -2: Py_RETURN_NONE;
		default: return NULL;
	}

	us_memsink_shared_s *mem = self->mem;
	us_frame_set_data(self->frame, mem->data, mem->used);
	US_FRAME_COPY_META(self->mem, self->frame);
	self->frame_id = mem->id;
	self->frame_ts = us_get_now_monotonic();
	mem->last_client_ts = self->frame_ts;
	if (key_required) {
		mem->key_requested = true;
	}

	if (flock(self->fd, LOCK_UN) < 0) {
		return PyErr_SetFromErrno(PyExc_OSError);
	}

	PyObject *dict_frame = PyDict_New();
	if (dict_frame  == NULL) {
		return NULL;
	}

#	define SET_VALUE(x_key, x_maker) { \
			PyObject *m_tmp = x_maker; \
			if (m_tmp == NULL) { \
				return NULL; \
			} \
			if (PyDict_SetItemString(dict_frame, x_key, m_tmp) < 0) { \
				Py_DECREF(m_tmp); \
				return NULL; \
			} \
			Py_DECREF(m_tmp); \
		}
#	define SET_NUMBER(x_key, x_from, x_to) SET_VALUE(#x_key, Py##x_to##_From##x_from(self->frame->x_key))

	SET_NUMBER(width, Long, Long);
	SET_NUMBER(height, Long, Long);
	SET_NUMBER(format, Long, Long);
	SET_NUMBER(stride, Long, Long);
	SET_NUMBER(online, Long, Bool);
	SET_NUMBER(key, Long, Bool);
	SET_NUMBER(gop, Long, Long);
	SET_NUMBER(grab_ts, Double, Float);
	SET_NUMBER(encode_begin_ts, Double, Float);
	SET_NUMBER(encode_end_ts, Double, Float);
	SET_VALUE("data", PyBytes_FromStringAndSize((const char *)self->frame->data, self->frame->used));

#	undef SET_NUMBER
#	undef SET_VALUE

	return dict_frame;
}

static PyObject *_MemsinkObject_is_opened(_MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	return PyBool_FromLong(self->mem != NULL && self->fd > 0);
}

#define FIELD_GETTER(x_field, x_from, x_to) \
	static PyObject *_MemsinkObject_getter_##x_field(_MemsinkObject *self, void *Py_UNUSED(closure)) { \
		return Py##x_to##_From##x_from(self->x_field); \
	}
FIELD_GETTER(obj, String, Unicode)
FIELD_GETTER(lock_timeout, Double, Float)
FIELD_GETTER(wait_timeout, Double, Float)
FIELD_GETTER(drop_same_frames, Double, Float)
#undef FIELD_GETTER

static PyMethodDef _MemsinkObject_methods[] = {
#	define ADD_METHOD(x_name, x_method, x_flags) \
		{.ml_name = x_name, .ml_meth = (PyCFunction)_MemsinkObject_##x_method, .ml_flags = (x_flags)}
	ADD_METHOD("close", close, METH_NOARGS),
	ADD_METHOD("__enter__", enter, METH_NOARGS),
	ADD_METHOD("__exit__", exit, METH_VARARGS),
	ADD_METHOD("wait_frame", wait_frame, METH_VARARGS | METH_KEYWORDS),
	ADD_METHOD("is_opened", is_opened, METH_NOARGS),
	{},
#	undef ADD_METHOD
};

static PyGetSetDef _MemsinkObject_getsets[] = {
#	define ADD_GETTER(x_field) {.name = #x_field, .get = (getter)_MemsinkObject_getter_##x_field}
	ADD_GETTER(obj),
	ADD_GETTER(lock_timeout),
	ADD_GETTER(wait_timeout),
	ADD_GETTER(drop_same_frames),
	{},
#	undef ADD_GETTER
};

static PyTypeObject _MemsinkType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name		= "ustreamer.Memsink",
	.tp_basicsize	= sizeof(_MemsinkObject),
	.tp_flags		= Py_TPFLAGS_DEFAULT,
	.tp_new			= PyType_GenericNew,
	.tp_init		= (initproc)_MemsinkObject_init,
	.tp_dealloc		= (destructor)_MemsinkObject_dealloc,
	.tp_repr		= (reprfunc)_MemsinkObject_repr,
	.tp_methods		= _MemsinkObject_methods,
	.tp_getset		= _MemsinkObject_getsets,
};

static PyModuleDef _Module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "ustreamer",
	.m_size = -1,
};

PyMODINIT_FUNC PyInit_ustreamer(void) {
	PyObject *module = PyModule_Create(&_Module);
	if (module == NULL) {
		return NULL;
	}

	if (PyType_Ready(&_MemsinkType) < 0) {
		return NULL;
	}

	Py_INCREF(&_MemsinkType);

	if (PyModule_AddObject(module, "Memsink", (PyObject *)&_MemsinkType) < 0) {
		return NULL;
	}

	return module;
}
