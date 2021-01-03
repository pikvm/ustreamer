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


#include "memsink.h"


static int _sem_timedwait_monotonic(sem_t *sem, long double timeout);
static int _flock_timedwait_monotonic(int fd, long double timeout);


memsink_s *memsink_init(const char *name, const char *prefix, bool server, mode_t mode, bool rm, unsigned timeout) {
	memsink_s *memsink;
	A_CALLOC(memsink, 1);
	memsink->name = name;
	memsink->server = server;
	memsink->rm = rm;
	memsink->timeout = timeout;
	memsink->fd = -1;
	memsink->mem = MAP_FAILED;
	memsink->sig_sem = SEM_FAILED;

	A_CALLOC(memsink->mem_name, strlen(prefix) + 8);
	A_CALLOC(memsink->sig_name, strlen(prefix) + 8);

	sprintf(memsink->mem_name, "%s.mem", prefix);
	sprintf(memsink->sig_name, "%s.sig", prefix);

	LOG_INFO("Using %s sink: %s.{mem,sig}", name, prefix);

	const int flags = (server ? O_RDWR | O_CREAT : O_RDWR);
#	define OPEN_SIGNAL { \
			if ((memsink->sig_sem = sem_open(memsink->sig_name, flags, mode, 0)) == SEM_FAILED) { \
				LOG_PERROR("Can't open %s sink signal semaphore", name); \
				goto error; \
			} \
		}

	if (!server) {
		OPEN_SIGNAL;
	}

	{ // Shared memory
		if ((memsink->fd = shm_open(memsink->mem_name, flags, mode)) == -1) {
			LOG_PERROR("Can't open %s sink memory", name);
			goto error;
		}

		if (memsink->server && ftruncate(memsink->fd, sizeof(memsink_shared_s)) < 0) {
			LOG_PERROR("Can't truncate %s sink memory", name);
			goto error;
		}

		if ((memsink->mem = mmap(
			NULL,
			sizeof(memsink_shared_s),
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			memsink->fd,
			0
		)) == MAP_FAILED) {
			LOG_PERROR("Can't mmap %s sink memory", name);
			goto error;
		}
	}

	if (server) {
		OPEN_SIGNAL;
	}

#	undef OPEN_SIGNAL

	return memsink;

	error:
		memsink_destroy(memsink);
		return NULL;
}

void memsink_destroy(memsink_s *memsink) {
	if (memsink->sig_sem != SEM_FAILED) {
		if (sem_close(memsink->sig_sem) < 0) {
			LOG_PERROR("Can't close %s sink signal semaphore", memsink->name);
		}
		if (memsink->rm && sem_unlink(memsink->sig_name) < 0) {
			if (errno != ENOENT) {
				LOG_PERROR("Can't remove %s sink signal semaphore", memsink->name);
			}
		}
	}

	if (memsink->mem != MAP_FAILED) {
		if (munmap(memsink->mem, sizeof(memsink_shared_s)) < 0) {
			LOG_PERROR("Can't unmap %s sink memory", memsink->name);
		}
	}

	if (memsink->fd >= 0) {
		if (close(memsink->fd) < 0) {
			LOG_PERROR("Can't close %s sink fd", memsink->name);
		}
		if (memsink->rm && shm_unlink(memsink->mem_name) < 0) {
			if (errno != ENOENT) {
				LOG_PERROR("Can't remove %s sink memory", memsink->name);
			}
		}
	}

	free(memsink->sig_name);
	free(memsink->mem_name);
	free(memsink);
}

int memsink_server_put(memsink_s *memsink, const frame_s *frame) {
	assert(memsink->server);

	const long double now = get_now_monotonic();

	if (frame->used > MEMSINK_MAX_DATA) {
		LOG_ERROR("%s sink: Can't put frame: is too big (%zu > %zu)",
			memsink->name, frame->used, MEMSINK_MAX_DATA);
		return 0; // -2
	}

	if (_flock_timedwait_monotonic(memsink->fd, 1) == 0) {
		LOG_PERF("%s sink: >>>>> Exposing new frame ...", memsink->name);

		if (sem_trywait(memsink->sig_sem) < 0 && errno != EAGAIN) {
			LOG_PERROR("%s sink: Can't wait signal semaphore", memsink->name);
			return -1;
		}

#		define COPY(_field) memsink->mem->_field = frame->_field
		COPY(used);
		COPY(width);
		COPY(height);
		COPY(format);
		COPY(online);
		COPY(grab_ts);
		COPY(encode_begin_ts);
		COPY(encode_end_ts);
		memcpy(memsink->mem->data, frame->data, frame->used);
#		undef COPY

		if (sem_post(memsink->sig_sem) < 0) {
			LOG_PERROR("%s sink: Can't post signal semaphore", memsink->name);
			return -1;
		}
		if (flock(memsink->fd, LOCK_UN) < 0) {
			LOG_PERROR("%s sink: Can't unlock memory", memsink->name);
			return -1;
		}
		LOG_VERBOSE("%s sink: Exposed new frame; full exposition time = %Lf",
			memsink->name, get_now_monotonic() - now);

	} else if (errno == EWOULDBLOCK) {
		LOG_PERF("%s sink: ===== Shared memory is busy now; frame skipped", memsink->name);

	} else {
		LOG_PERROR("%s sink: Can't lock memory", memsink->name);
		return -1;
	}
	return 0;
}

int memsink_client_get(memsink_s *memsink, frame_s *frame) { // cppcheck-suppress unusedFunction
	assert(!memsink->server); // Client only

	if (_sem_timedwait_monotonic(memsink->sig_sem, memsink->timeout) < 0) {
		if (errno == EAGAIN) {
			return -2;
		}
		LOG_PERROR("%s src: Can't wait signal semaphore", memsink->name);
		return -1;
	}
	if (_flock_timedwait_monotonic(memsink->fd, memsink->timeout) < 0) {
		if (errno == EWOULDBLOCK) {
			return -2;
		}
		LOG_PERROR("%s src: Can't lock memory", memsink->name);
		return -1;
	}

#	define COPY(_field) frame->_field = memsink->mem->_field
	COPY(width);
	COPY(height);
	COPY(format);
	COPY(online);
	COPY(grab_ts);
	COPY(encode_begin_ts);
	COPY(encode_end_ts);
	frame_set_data(frame, memsink->mem->data, memsink->mem->used);
#	undef COPY

	if (flock(memsink->fd, LOCK_UN) < 0) {
		LOG_PERROR("%s src: Can't unlock memory", memsink->name);
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
