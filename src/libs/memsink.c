/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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
	memsink_s *sink;
	A_CALLOC(sink, 1);
	sink->name = name;
	sink->server = server;
	sink->rm = rm;
	sink->timeout = timeout;
	sink->fd = -1;
	sink->mem = MAP_FAILED;
	sink->sig_sem = SEM_FAILED;

	A_CALLOC(sink->mem_name, strlen(prefix) + 8);
	A_CALLOC(sink->sig_name, strlen(prefix) + 8);

	sprintf(sink->mem_name, "%s.mem", prefix);
	sprintf(sink->sig_name, "%s.sig", prefix);

	LOG_INFO("Using '%s' sink: %s.{mem,sig}", name, prefix);

	const int flags = (server ? O_RDWR | O_CREAT : O_RDWR);
#	define OPEN_SIGNAL { \
			if ((sink->sig_sem = sem_open(sink->sig_name, flags, mode, 0)) == SEM_FAILED) { \
				LOG_PERROR("Can't open %s sink signal semaphore", name); \
				goto error; \
			} \
		}

	if (!server) {
		OPEN_SIGNAL;
	}

	{ // Shared memory
		if ((sink->fd = shm_open(sink->mem_name, flags, mode)) == -1) {
			LOG_PERROR("Can't open %s sink memory", name);
			goto error;
		}

		if (sink->server && ftruncate(sink->fd, sizeof(memsink_shared_s)) < 0) {
			LOG_PERROR("Can't truncate %s sink memory", name);
			goto error;
		}

		if ((sink->mem = mmap(
			NULL,
			sizeof(memsink_shared_s),
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			sink->fd,
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

	return sink;

	error:
		memsink_destroy(sink);
		return NULL;
}

void memsink_destroy(memsink_s *sink) {
	if (sink->sig_sem != SEM_FAILED) {
		if (sem_close(sink->sig_sem) < 0) {
			LOG_PERROR("Can't close %s sink signal semaphore", sink->name);
		}
		if (sink->rm && sem_unlink(sink->sig_name) < 0) {
			if (errno != ENOENT) {
				LOG_PERROR("Can't remove %s sink signal semaphore", sink->name);
			}
		}
	}

	if (sink->mem != MAP_FAILED) {
		if (munmap(sink->mem, sizeof(memsink_shared_s)) < 0) {
			LOG_PERROR("Can't unmap %s sink memory", sink->name);
		}
	}

	if (sink->fd >= 0) {
		if (close(sink->fd) < 0) {
			LOG_PERROR("Can't close %s sink fd", sink->name);
		}
		if (sink->rm && shm_unlink(sink->mem_name) < 0) {
			if (errno != ENOENT) {
				LOG_PERROR("Can't remove %s sink memory", sink->name);
			}
		}
	}

	free(sink->sig_name);
	free(sink->mem_name);
	free(sink);
}

int memsink_server_put(memsink_s *sink, const frame_s *frame) {
	assert(sink->server);

	const long double now = get_now_monotonic();

	if (frame->used > MEMSINK_MAX_DATA) {
		LOG_ERROR("%s sink: Can't put frame: is too big (%zu > %zu)",
			sink->name, frame->used, MEMSINK_MAX_DATA);
		return 0; // -2
	}

	if (_flock_timedwait_monotonic(sink->fd, 1) == 0) {
		LOG_VERBOSE("%s sink: >>>>> Exposing new frame ...", sink->name);

		if (sem_trywait(sink->sig_sem) < 0 && errno != EAGAIN) {
			LOG_PERROR("%s sink: Can't wait signal semaphore", sink->name);
			return -1;
		}

#		define COPY(_field) sink->mem->_field = frame->_field
		COPY(used);
		COPY(width);
		COPY(height);
		COPY(format);
		COPY(stride);
		COPY(online);
		COPY(grab_ts);
		COPY(encode_begin_ts);
		COPY(encode_end_ts);
		memcpy(sink->mem->data, frame->data, frame->used);
#		undef COPY

		if (sem_post(sink->sig_sem) < 0) {
			LOG_PERROR("%s sink: Can't post signal semaphore", sink->name);
			return -1;
		}
		if (flock(sink->fd, LOCK_UN) < 0) {
			LOG_PERROR("%s sink: Can't unlock memory", sink->name);
			return -1;
		}
		LOG_VERBOSE("%s sink: Exposed new frame; full exposition time = %Lf",
			sink->name, get_now_monotonic() - now);

	} else if (errno == EWOULDBLOCK) {
		LOG_VERBOSE("%s sink: ===== Shared memory is busy now; frame skipped", sink->name);

	} else {
		LOG_PERROR("%s sink: Can't lock memory", sink->name);
		return -1;
	}
	return 0;
}

int memsink_client_get(memsink_s *sink, frame_s *frame) { // cppcheck-suppress unusedFunction
	assert(!sink->server); // Client only

	if (_sem_timedwait_monotonic(sink->sig_sem, sink->timeout) < 0) {
		if (errno == EAGAIN) {
			return -2;
		}
		LOG_PERROR("%s sink: Can't wait signal semaphore", sink->name);
		return -1;
	}
	if (_flock_timedwait_monotonic(sink->fd, sink->timeout) < 0) {
		if (errno == EWOULDBLOCK) {
			return -2;
		}
		LOG_PERROR("%s sink: Can't lock memory", sink->name);
		return -1;
	}

#	define COPY(_field) frame->_field = sink->mem->_field
	COPY(width);
	COPY(height);
	COPY(format);
	COPY(stride);
	COPY(online);
	COPY(grab_ts);
	COPY(encode_begin_ts);
	COPY(encode_end_ts);
	frame_set_data(frame, sink->mem->data, sink->mem->used);
#	undef COPY

	if (flock(sink->fd, LOCK_UN) < 0) {
		LOG_PERROR("%s sink: Can't unlock memory", sink->name);
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
