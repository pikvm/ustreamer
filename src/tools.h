/*****************************************************************************
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


#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "logging.h"


#define A_PTHREAD_CREATE(_tid, _func, _arg)	assert(!pthread_create(_tid, NULL, _func, _arg))
#define A_PTHREAD_JOIN(_tid)				assert(!pthread_join(_tid, NULL))

#define A_PTHREAD_M_INIT(_mutex)	assert(!pthread_mutex_init(_mutex, NULL))
#define A_PTHREAD_M_DESTROY(_mutex)	assert(!pthread_mutex_destroy(_mutex))
#define A_PTHREAD_M_LOCK(_mutex)	assert(!pthread_mutex_lock(_mutex))
#define A_PTHREAD_M_UNLOCK(_mutex)	assert(!pthread_mutex_unlock(_mutex))

#define A_PTHREAD_C_INIT(_cond)		assert(!pthread_cond_init(_cond, NULL))
#define A_PTHREAD_C_DESTROY(_cond)	assert(!pthread_cond_destroy(_cond))
#define A_PTHREAD_C_SIGNAL(...)		assert(!pthread_cond_signal(__VA_ARGS__))
#define A_PTHREAD_C_WAIT_TRUE(_var, _cond, _mutex) { while(!_var) assert(!pthread_cond_wait(_cond, _mutex)); }


#define A_CALLOC(_dest, _nmemb)		assert((_dest = calloc(_nmemb, sizeof(*(_dest)))))
#define A_REALLOC(_dest, _nmemb)	assert((_dest = realloc(_dest, _nmemb * sizeof(*(_dest)))))
#define MEMSET_ZERO(_x_obj)			memset(&(_x_obj), 0, sizeof(_x_obj))
#define MEMSET_ZERO_PTR(_x_ptr)		memset(_x_ptr, 0, sizeof(*(_x_ptr)))


#define INLINE inline __attribute__((always_inline))
#define UNUSED __attribute__((unused))


#define XIOCTL_RETRIES 4


INLINE unsigned max_u(unsigned a, unsigned b) {
	return (a > b ? a : b);
}

INLINE void now_ms(time_t *sec, long *msec) {
	struct timespec spec;

	assert(!clock_gettime(CLOCK_MONOTONIC_RAW, &spec));
	*sec = spec.tv_sec;
	*msec = round(spec.tv_nsec / 1.0e6);

	if (*msec > 999) {
		*sec += 1;
		*msec = 0;
	}
}

INLINE long double now_ms_ld(void) {
	time_t sec;
	long msec;

	now_ms(&sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE int xioctl(const int fd, const int request, void *arg) {
	int retries = XIOCTL_RETRIES;
	int retval = -1;

	do {
		retval = ioctl(fd, request, arg);
	} while (
		retval
		&& retries--
		&& (
			errno == EINTR
			|| errno == EAGAIN
			|| errno == ETIMEDOUT
		)
	);

	if (retval && retries <= 0) {
		LOG_PERROR("ioctl(%d) retried %d times; giving up", request, XIOCTL_RETRIES);
	}
	return retval;
}
