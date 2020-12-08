/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#include "rawsink.h"


static int _sem_wait_monotonic(sem_t *sem, long double timeout);


struct rawsink_t *rawsink_init(const char *name, mode_t mode, bool rm, bool master) {
	struct rawsink_t *rawsink;
	int flags = (master ? O_RDWR | O_CREAT : O_RDWR);

	A_CALLOC(rawsink, 1);
	rawsink->fd = -1;
	rawsink->shared = MAP_FAILED;
	rawsink->signal_sem = SEM_FAILED;
	rawsink->lock_sem = SEM_FAILED;
	rawsink->rm = rm;
	rawsink->master = master;

	A_CALLOC(rawsink->mem_name, strlen(name) + 8);
	A_CALLOC(rawsink->signal_name, strlen(name) + 8);
	A_CALLOC(rawsink->lock_name, strlen(name) + 8);

	sprintf(rawsink->mem_name, "%s.mem", name);
	sprintf(rawsink->signal_name, "%s.sig", name);
	sprintf(rawsink->lock_name, "%s.lock", name);

	LOG_INFO("Using RAW sink: %s.{mem,sig,lock}", name);

#	define OPEN_SEM(_role, _default) { \
			if ((rawsink->_role##_sem = sem_open(rawsink->_role##_name, flags, mode, _default)) == SEM_FAILED) { \
				LOG_PERROR("Can't open RAW sink " #_role " semaphore"); \
				goto error; \
			} \
		}

	if (!master) {
		OPEN_SEM(lock, 1);
		OPEN_SEM(signal, 0);
	}

	{ // Shared memory
		if ((rawsink->fd = shm_open(rawsink->mem_name, flags, mode)) == -1) {
			LOG_PERROR("Can't open RAW sink memory");
			goto error;
		}

		if (ftruncate(rawsink->fd, sizeof(struct rawsink_shared_t)) < 0) {
			LOG_PERROR("Can't truncate RAW sink memory");
			goto error;
		}

		if ((rawsink->shared = mmap(
			NULL,
			sizeof(struct rawsink_shared_t),
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			rawsink->fd,
			0
		)) == MAP_FAILED) {
			LOG_PERROR("Can't mmap RAW sink memory");
			goto error;
		}
	}

	if (master) {
		OPEN_SEM(signal, 0);
		OPEN_SEM(lock, 1);
	}

#	undef OPEN_SEM

	return rawsink;

	error:
		rawsink_destroy(rawsink);
		return NULL;
}

void rawsink_destroy(struct rawsink_t *rawsink) {
#	define CLOSE_SEM(_role) { \
			if (rawsink->_role##_sem != SEM_FAILED) { \
				if (sem_close(rawsink->_role##_sem) < 0) { \
					LOG_PERROR("Can't close RAW sink " #_role " semaphore"); \
				} \
				if (rawsink->rm && sem_unlink(rawsink->_role##_name) < 0) { \
					if (errno != ENOENT) { \
						LOG_PERROR("Can't remove RAW sink " #_role " semaphore"); \
					} \
				} \
			} \
		}

	CLOSE_SEM(lock);
	CLOSE_SEM(signal);

#	undef CLOSE_SEM

	if (rawsink->shared != MAP_FAILED) {
		if (munmap(rawsink->shared, sizeof(struct rawsink_shared_t)) < 0) {
			LOG_PERROR("Can't unmap RAW sink memory");
		}
	}

	if (rawsink->fd >= 0) {
		if (close(rawsink->fd) < 0) {
			LOG_PERROR("Can't close RAW sink fd");
		}
		if (rawsink->rm && shm_unlink(rawsink->mem_name) < 0) {
			if (errno != ENOENT) {
				LOG_PERROR("Can't remove RAW sink memory");
			}
		}
	}

	free(rawsink->lock_name);
	free(rawsink->signal_name);
	free(rawsink->mem_name);
	free(rawsink);
}

void rawsink_put(
	struct rawsink_t *rawsink,
	const unsigned char *data, size_t size,
	unsigned format, unsigned width, unsigned height,
	long double grab_ts) {

	long double now = get_now_monotonic();

	assert(rawsink->master); // Master only

	if (rawsink->master_failed) {
		return;
	}

	if (size > RAWSINK_MAX_DATA) {
		LOG_ERROR("RAWSINK: Can't put RAW frame: is too big (%zu > %zu)", size, RAWSINK_MAX_DATA);
		return;
	}

	if (sem_trywait(rawsink->lock_sem) == 0) {
		LOG_PERF("RAWSINK: >>>>> Exposing new frame ...");

		if (sem_trywait(rawsink->signal_sem) < 0 && errno != EAGAIN) {
			LOG_PERROR("RAWSINK: Can't wait %s", rawsink->signal_name);
			goto error;
		}

#		define COPY(_field) rawsink->shared->_field = _field
		COPY(format);
		COPY(width);
		COPY(height);
		COPY(grab_ts);
		COPY(size);
		memcpy(rawsink->shared->data, data, size);
#		undef COPY

		if (sem_post(rawsink->signal_sem) < 0) {
			LOG_PERROR("RAWSINK: Can't post %s", rawsink->signal_name);
			goto error;
		}
		if (sem_post(rawsink->lock_sem) < 0) {
			LOG_PERROR("RAWSINK: Can't post %s", rawsink->lock_name);
			goto error;
		}
		LOG_VERBOSE("RAWSINK: Exposed new frame; full exposition time = %Lf", get_now_monotonic() - now);

	} else if (errno == EAGAIN) {
		LOG_PERF("RAWSINK: ===== Shared memory is busy now; frame skipped");

	} else {
		LOG_PERROR("RAWSINK: Can't wait %s", rawsink->lock_name);
		goto error;
	}

	return;

	error:
		LOG_ERROR("RAW sink completely disabled due error");
		rawsink->master_failed = true;
}

int rawsink_get(
	struct rawsink_t *rawsink,
	char *data, size_t *size,
	unsigned *format, unsigned *width, unsigned *height,
	long double *grab_ts,
	long double timeout) {

	assert(!rawsink->master); // Slave only

#	define WAIT_SEM(_role) { \
			if (_sem_wait_monotonic(rawsink->_role##_sem, timeout) < 0) { \
				if (errno == EAGAIN) { \
					return -2; \
				} \
				LOG_PERROR("RAWSRC: Can't wait %s", rawsink->_role##_name); \
				return -1; \
			} \
		}

	WAIT_SEM(signal);
	WAIT_SEM(lock);

#	define COPY(_field) *_field = rawsink->shared->_field
	COPY(format);
	COPY(width);
	COPY(height);
	COPY(grab_ts);
	COPY(size);
	memcpy(data, rawsink->shared->data, *size);
#	undef COPY

	if (sem_post(rawsink->lock_sem) < 0) {
		LOG_PERROR("RAWSINK: Can't post %s", rawsink->lock_name);
		return -1;
	}
	return 0;
}

static int _sem_wait_monotonic(sem_t *sem, long double timeout) {
	long double deadline_ts = get_now_monotonic() + timeout;
	int retval = -1;

	while (true) {
		retval = sem_trywait(sem);
		if (retval == 0 || retval != EAGAIN || get_now_monotonic() > deadline_ts) {
			break;
		}
		usleep(1000);
	}
	return retval;
}
