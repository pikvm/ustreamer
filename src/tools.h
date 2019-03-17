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


#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

#include <sys/ioctl.h>
#include <sys/syscall.h>


#define A_THREAD_CREATE(_tid, _func, _arg)	assert(!pthread_create(_tid, NULL, _func, _arg))
#define A_THREAD_JOIN(_tid)					assert(!pthread_join(_tid, NULL))

#define A_MUTEX_INIT(_mutex)	assert(!pthread_mutex_init(_mutex, NULL))
#define A_MUTEX_DESTROY(_mutex)	assert(!pthread_mutex_destroy(_mutex))
#define A_MUTEX_LOCK(_mutex)	assert(!pthread_mutex_lock(_mutex))
#define A_MUTEX_UNLOCK(_mutex)	assert(!pthread_mutex_unlock(_mutex))

#define A_COND_INIT(_cond)		assert(!pthread_cond_init(_cond, NULL))
#define A_COND_DESTROY(_cond)	assert(!pthread_cond_destroy(_cond))
#define A_COND_SIGNAL(...)		assert(!pthread_cond_signal(__VA_ARGS__))
#define A_COND_WAIT_TRUE(_var, _cond, _mutex) { while(!_var) assert(!pthread_cond_wait(_cond, _mutex)); }

#define A_CALLOC(_dest, _nmemb)		assert((_dest = calloc(_nmemb, sizeof(*(_dest)))))
#define A_REALLOC(_dest, _nmemb)	assert((_dest = realloc(_dest, _nmemb * sizeof(*(_dest)))))
#define MEMSET_ZERO(_obj)			memset(&(_obj), 0, sizeof(_obj))

#define ARRAY_LEN(_array) (sizeof(_array) / sizeof(_array[0]))

#define INLINE inline __attribute__((always_inline))
#define UNUSED __attribute__((unused))


INLINE char *bool_to_string(bool flag) {
	return (flag ? "true" : "false");
}

INLINE unsigned min_u(unsigned a, unsigned b) {
	return (a < b ? a : b);
}

INLINE unsigned max_u(unsigned a, unsigned b) {
	return (a > b ? a : b);
}

INLINE long long floor_ms(long double now) {
	return (long long)now - (now < (long long)now); // floor()
}

INLINE void get_now(clockid_t clk_id, time_t *sec, long *msec) {
	struct timespec spec;

	assert(!clock_gettime(clk_id, &spec));
	*sec = spec.tv_sec;
	*msec = round(spec.tv_nsec / 1.0e6);

	if (*msec > 999) {
		*sec += 1;
		*msec = 0;
	}
}

INLINE long double get_now_monotonic(void) {
	time_t sec;
	long msec;

	get_now(CLOCK_MONOTONIC_RAW, &sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE long double get_now_real(void) {
	time_t sec;
	long msec;

	get_now(CLOCK_REALTIME, &sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}
