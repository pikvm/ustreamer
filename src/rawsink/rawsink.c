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


static int _sem_timedwait_monotonic(sem_t *sem, long double timeout);
static int _flock_timedwait_monotonic(int fd, long double timeout);


rawsink_s *rawsink_open(const char *name, bool server, mode_t mode, bool rm, unsigned timeout) {
	rawsink_s *rawsink;
	int flags = (server ? O_RDWR | O_CREAT : O_RDWR);

	A_CALLOC(rawsink, 1);
	rawsink->server = server;
	rawsink->rm = rm;
	rawsink->timeout = timeout;
	rawsink->fd = -1;
	rawsink->mem = MAP_FAILED;
	rawsink->sig_sem = SEM_FAILED;

	A_CALLOC(rawsink->mem_name, strlen(name) + 8);
	A_CALLOC(rawsink->sig_name, strlen(name) + 8);

	sprintf(rawsink->mem_name, "%s.mem", name);
	sprintf(rawsink->sig_name, "%s.sig", name);

	LOG_INFO("Using RAW sink: %s.{mem,sig}", name);

#	define OPEN_SIGNAL { \
			if ((rawsink->sig_sem = sem_open(rawsink->sig_name, flags, mode, 0)) == SEM_FAILED) { \
				LOG_PERROR("Can't open RAW sink signal semaphore"); \
				goto error; \
			} \
		}

	if (!server) {
		OPEN_SIGNAL;
	}

	{ // Shared memory
		if ((rawsink->fd = shm_open(rawsink->mem_name, flags, mode)) == -1) {
			LOG_PERROR("Can't open RAW sink memory");
			goto error;
		}

		if (rawsink->server && ftruncate(rawsink->fd, sizeof(rawsink_shared_s)) < 0) {
			LOG_PERROR("Can't truncate RAW sink memory");
			goto error;
		}

		if ((rawsink->mem = mmap(
			NULL,
			sizeof(rawsink_shared_s),
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			rawsink->fd,
			0
		)) == MAP_FAILED) {
			LOG_PERROR("Can't mmap RAW sink memory");
			goto error;
		}
	}

	if (server) {
		OPEN_SIGNAL;
	}

#	undef OPEN_SIGNAL

	return rawsink;

	error:
		rawsink_close(rawsink);
		return NULL;
}

void rawsink_close(rawsink_s *rawsink) {
	if (rawsink->sig_sem != SEM_FAILED) {
		if (sem_close(rawsink->sig_sem) < 0) {
			LOG_PERROR("Can't close RAW sink signal semaphore");
		}
		if (rawsink->rm && sem_unlink(rawsink->sig_name) < 0) {
			if (errno != ENOENT) {
				LOG_PERROR("Can't remove RAW sink signal semaphore");
			}
		}
	}

	if (rawsink->mem != MAP_FAILED) {
		if (munmap(rawsink->mem, sizeof(rawsink_shared_s)) < 0) {
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

	free(rawsink->sig_name);
	free(rawsink->mem_name);
	free(rawsink);
}

int rawsink_server_put(rawsink_s *rawsink, frame_s *frame) {
	long double now = get_now_monotonic();

	assert(rawsink->server);

	if (frame->used > RAWSINK_MAX_DATA) {
		LOG_ERROR("RAWSINK: Can't put RAW frame: is too big (%zu > %zu)", frame->used, RAWSINK_MAX_DATA);
		return 0; // -2
	}

	if (_flock_timedwait_monotonic(rawsink->fd, 1) == 0) {
		LOG_PERF("RAWSINK: >>>>> Exposing new frame ...");

		if (sem_trywait(rawsink->sig_sem) < 0 && errno != EAGAIN) {
			LOG_PERROR("RAWSINK: Can't wait signal semaphore");
			return -1;
		}

#		define COPY(_field) rawsink->mem->_field = frame->_field
		COPY(used);
		COPY(width);
		COPY(height);
		COPY(format);
		COPY(online);
		COPY(grab_ts);
		memcpy(rawsink->mem->data, frame->data, frame->used);
#		undef COPY

		if (sem_post(rawsink->sig_sem) < 0) {
			LOG_PERROR("RAWSINK: Can't post signal semaphore");
			return -1;
		}
		if (flock(rawsink->fd, LOCK_UN) < 0) {
			LOG_PERROR("RAWSINK: Can't unlock memory");
			return -1;
		}
		LOG_VERBOSE("RAWSINK: Exposed new frame; full exposition time = %Lf", get_now_monotonic() - now);

	} else if (errno == EWOULDBLOCK) {
		LOG_PERF("RAWSINK: ===== Shared memory is busy now; frame skipped");

	} else {
		LOG_PERROR("RAWSINK: Can't lock memory");
		return -1;
	}
	return 0;
}

int rawsink_client_get(rawsink_s *rawsink, frame_s *frame) { // cppcheck-suppress unusedFunction
	assert(!rawsink->server); // Client only

	if (_sem_timedwait_monotonic(rawsink->sig_sem, rawsink->timeout) < 0) {
		if (errno == EAGAIN) {
			return -2;
		}
		LOG_PERROR("RAWSRC: Can't wait signal semaphore");
		return -1;
	}
	if (_flock_timedwait_monotonic(rawsink->fd, rawsink->timeout) < 0) {
		if (errno == EWOULDBLOCK) {
			return -2;
		}
		LOG_PERROR("RAWSRC: Can't lock memory");
		return -1;
	}

#	define COPY(_field) frame->_field = rawsink->mem->_field
	COPY(width);
	COPY(height);
	COPY(format);
	COPY(online);
	COPY(grab_ts);
	frame_set_data(frame, rawsink->mem->data, rawsink->mem->used);
#	undef COPY

	if (flock(rawsink->fd, LOCK_UN) < 0) {
		LOG_PERROR("RAWSRC: Can't unlock memory");
		return -1;
	}
	return 0;
}

static int _sem_timedwait_monotonic(sem_t *sem, long double timeout) {
	long double deadline_ts = get_now_monotonic() + timeout;
	int retval = -1;

	while (true) {
		retval = sem_trywait(sem);
		if (retval == 0 || errno != EAGAIN || get_now_monotonic() > deadline_ts) {
			break;
		}
		usleep(1000);
	}
	return retval;
}

static int _flock_timedwait_monotonic(int fd, long double timeout) {
	long double deadline_ts = get_now_monotonic() + timeout;
	int retval = -1;

	while (true) {
		retval = flock(fd, LOCK_EX | LOCK_NB);
		if (retval == 0 || errno != EWOULDBLOCK || get_now_monotonic() > deadline_ts) {
			break;
		}
		usleep(1000);
	}
	return retval;
}
