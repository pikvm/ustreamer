#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <Python.h>

#include "../libs/tools.h" // Just a header without C-sources
#include "../libs/memsinksh.h" // No sources again


typedef struct {
	PyObject_HEAD

	char	*obj;
	double	lock_timeout;
	double	wait_timeout;

	int					fd;
	memsink_shared_s	*mem;
	uint8_t				*tmp_data;
	size_t				tmp_data_allocated;
	uint64_t			last_id;
} MemsinkObject;


static void MemsinkObject_destroy_internals(MemsinkObject *self) {
	if (self->mem != NULL) {
		munmap(self->mem, sizeof(memsink_shared_s));
		self->mem = NULL;
	}
	if (self->fd > 0) {
		close(self->fd);
		self->fd = -1;
	}
	if (self->tmp_data) {
		free(self->tmp_data);
		self->tmp_data = NULL;
		self->tmp_data_allocated = 0;
	}
}

static int MemsinkObject_init(MemsinkObject *self, PyObject *args, PyObject *kwargs) {
	self->lock_timeout = 1;
	self->wait_timeout = 1;

	static char *kws[] = {"obj", "lock_timeout", "wait_timeout", NULL};
	if (!PyArg_ParseTupleAndKeywords(
		args, kwargs, "s|dd", kws,
		&self->obj, &self->lock_timeout, &self->wait_timeout)) {
		return -1;
	}

#	define SET_TIMEOUT(_timeout) { \
			if (self->_timeout <= 0) { \
				PyErr_SetString(PyExc_ValueError, #_timeout " must be > 0"); \
				return -1; \
			} \
		}

	SET_TIMEOUT(lock_timeout);
	SET_TIMEOUT(wait_timeout);

#	undef CHECK_TIMEOUT

	self->tmp_data_allocated = 512 * 1024;
	A_REALLOC(self->tmp_data, self->tmp_data_allocated);

	if ((self->fd = shm_open(self->obj, O_RDWR, 0)) == -1) {
		PyErr_SetFromErrno(PyExc_OSError);
		goto error;
	}

	if ((self->mem = mmap(
		NULL,
		sizeof(memsink_shared_s),
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		self->fd,
		0
	)) == MAP_FAILED) {
		PyErr_SetFromErrno(PyExc_OSError);
		self->mem = NULL;
		goto error;
	}
	if (self->mem == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "Memory mapping is NULL"); \
		goto error;
	}

	return 0;

	error:
		MemsinkObject_destroy_internals(self);
		return -1;
}

static PyObject *MemsinkObject_repr(MemsinkObject *self) {
	char repr[1024];
	snprintf(repr, 1023, "<Memsink(%s)>", self->obj);
	return Py_BuildValue("s", repr);
}

static void MemsinkObject_dealloc(MemsinkObject *self) {
	MemsinkObject_destroy_internals(self);
	PyObject_Del(self);
}

static PyObject *MemsinkObject_close(MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	MemsinkObject_destroy_internals(self);
	Py_RETURN_NONE;
}

static PyObject *MemsinkObject_enter(MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	Py_INCREF(self);
	return (PyObject *)self;
}

static PyObject *MemsinkObject_exit(MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	return PyObject_CallMethod((PyObject *)self, "close", "");
}

static PyObject *MemsinkObject_wait_frame(MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	if (self->mem == NULL || self->fd <= 0) {
		PyErr_SetString(PyExc_RuntimeError, "Closed");
		return NULL;
	}

	bool found = false;
	bool failed = false;

	Py_BEGIN_ALLOW_THREADS
	long double deadline_ts = get_now_monotonic() + self->wait_timeout;
	do {
		int retval = flock_timedwait_monotonic(self->fd, self->lock_timeout);
		if (retval < 0 && errno != EWOULDBLOCK) {
			failed = true;
			break;
		} else if (retval == 0) {
			if (
				self->mem->magic == MEMSINK_MAGIC
				&& self->mem->version == MEMSINK_VERSION
				&& self->mem->id != self->last_id
			) {
				found = true;
				break;
			}
			if (flock(self->fd, LOCK_UN) < 0) {
				failed = true;
				break;
			}
		}
		errno = 0;
		usleep(1000);
		if (errno == EINTR) {
			failed = true;
			break;
		}
	} while (get_now_monotonic() < deadline_ts);
	Py_END_ALLOW_THREADS

	if (failed) {
		return PyErr_SetFromErrno(PyExc_OSError);
	}
	if (!found) {
		Py_RETURN_NONE;
	}

#	define COPY(_type, _field) _type tmp_##_field = self->mem->_field
	COPY(unsigned, width);
	COPY(unsigned, height);
	COPY(unsigned, format);
	COPY(unsigned, stride);
	COPY(bool, online);
	COPY(double, grab_ts);
	COPY(double, encode_begin_ts);
	COPY(double, encode_end_ts);
	COPY(unsigned, used);
#	undef COPY

	// Временный буффер используется для скорейшего разблокирования синка
	if (self->tmp_data_allocated < self->mem->used) {
		size_t size = self->mem->used + (512 * 1024);
		A_REALLOC(self->tmp_data, size);
		self->tmp_data_allocated = size;
	}
	memcpy(self->tmp_data, self->mem->data, self->mem->used);

	self->mem->last_client_ts = get_now_monotonic();
	self->last_id = self->mem->id;

	if (flock(self->fd, LOCK_UN) < 0) {
		return PyErr_SetFromErrno(PyExc_OSError);
	}

	PyObject *frame = PyDict_New();
#	define SET_VALUE(_field, _from, _to) PyDict_SetItemString(frame, #_field, Py##_to##_From##_from(tmp_##_field))
	SET_VALUE(width, Long, Long);
	SET_VALUE(height, Long, Long);
	SET_VALUE(format, Long, Long);
	SET_VALUE(stride, Long, Long);
	SET_VALUE(online, Long, Bool);
	SET_VALUE(grab_ts, Double, Float);
	SET_VALUE(encode_begin_ts, Double, Float);
	SET_VALUE(encode_end_ts, Double, Float);
#	undef SET_VALUE
	PyDict_SetItemString(frame, "data", PyBytes_FromStringAndSize((const char *)self->tmp_data, tmp_used));
	return frame;
}

static PyObject *MemsinkObject_is_opened(MemsinkObject *self, PyObject *Py_UNUSED(ignored)) {
	return PyBool_FromLong(self->mem != NULL && self->fd > 0);
}

#define FIELD_GETTER(_field, _from, _to) \
	static PyObject *MemsinkObject_getter_##_field(MemsinkObject *self, void *Py_UNUSED(closure)) { \
		return Py##_to##_From##_from(self->_field); \
	}

FIELD_GETTER(obj, String, Unicode)
FIELD_GETTER(lock_timeout, Double, Float)
FIELD_GETTER(wait_timeout, Double, Float)

#undef FIELD_GETTER

static PyMethodDef MemsinkObject_methods[] = {
#	define ADD_METHOD(_meth, _flags) {.ml_name = #_meth, .ml_meth = (PyCFunction)MemsinkObject_##_meth, .ml_flags = (_flags)}
	ADD_METHOD(close, METH_NOARGS),
	ADD_METHOD(enter, METH_NOARGS),
	ADD_METHOD(exit, METH_VARARGS),
	ADD_METHOD(wait_frame, METH_NOARGS),
	ADD_METHOD(is_opened, METH_NOARGS),
	{},
#	undef ADD_METHOD
};

static PyGetSetDef MemsinkObject_getsets[] = {
#	define ADD_GETTER(_field) {.name = #_field, .get = (getter)MemsinkObject_getter_##_field}
	ADD_GETTER(obj),
	ADD_GETTER(lock_timeout),
	ADD_GETTER(wait_timeout),
	{},
#	undef ADD_GETTER
};

static PyTypeObject MemsinkType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name		= "ustreamer.Memsink",
	.tp_basicsize	= sizeof(MemsinkObject),
	.tp_flags		= Py_TPFLAGS_DEFAULT,
	.tp_new			= PyType_GenericNew,
	.tp_init		= (initproc)MemsinkObject_init,
	.tp_dealloc		= (destructor)MemsinkObject_dealloc,
	.tp_repr		= (reprfunc)MemsinkObject_repr,
	.tp_methods		= MemsinkObject_methods,
	.tp_getset		= MemsinkObject_getsets,
};

static PyModuleDef ustreamer_Module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "ustreamer",
	.m_size = -1,
};

PyMODINIT_FUNC PyInit_ustreamer(void) { // cppcheck-suppress unusedFunction
	PyObject *module = PyModule_Create(&ustreamer_Module);
	if (module == NULL) {
		return NULL;
	}

	if (PyType_Ready(&MemsinkType) < 0) {
		return NULL;
	}

	Py_INCREF(&MemsinkType);

	if (PyModule_AddObject(module, "Memsink", (PyObject *)&MemsinkType) < 0) {
		return NULL;
	}

	return module;
}
