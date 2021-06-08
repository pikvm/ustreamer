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


#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <assert.h>

#include <sys/file.h>


#define RN "\r\n"

#define INLINE inline __attribute__((always_inline))
#define UNUSED __attribute__((unused))

#define A_CALLOC(_dest, _nmemb)		assert((_dest = calloc(_nmemb, sizeof(*(_dest)))))
#define A_REALLOC(_dest, _nmemb)	assert((_dest = realloc(_dest, _nmemb * sizeof(*(_dest)))))
#define MEMSET_ZERO(_obj)			memset(&(_obj), 0, sizeof(_obj))

#define A_ASPRINTF(_dest, _fmt, ...) assert(asprintf(&(_dest), _fmt, ##__VA_ARGS__) >= 0)

#define ARRAY_LEN(_array) (sizeof(_array) / sizeof(_array[0]))


INLINE const char *bool_to_string(bool flag) {
	return (flag ? "true" : "false");
}

INLINE size_t align_size(size_t size, size_t to) {
	return ((size + (to - 1)) & ~(to - 1));
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

INLINE uint32_t triple_u32(uint32_t x) {
	// https://nullprogram.com/blog/2018/07/31/
	x ^= x >> 17;
	x *= UINT32_C(0xED5AD4BB);
	x ^= x >> 11;
	x *= UINT32_C(0xAC4C1B51);
	x ^= x >> 15;
	x *= UINT32_C(0x31848BAB);
	x ^= x >> 14;
	return x;
}

INLINE void get_now(clockid_t clk_id, time_t *sec, long *msec) {
	struct timespec ts;
	assert(!clock_gettime(clk_id, &ts));
	*sec = ts.tv_sec;
	*msec = round(ts.tv_nsec / 1.0e6);

	if (*msec > 999) {
		*sec += 1;
		*msec = 0;
	}
}

#if defined(CLOCK_MONOTONIC_RAW)
#	define X_CLOCK_MONOTONIC CLOCK_MONOTONIC_RAW
#elif defined(CLOCK_MONOTONIC_FAST)
#	define X_CLOCK_MONOTONIC CLOCK_MONOTONIC_FAST
#else
#	define X_CLOCK_MONOTONIC CLOCK_MONOTONIC
#endif

INLINE long double get_now_monotonic(void) {
	time_t sec;
	long msec;
	get_now(X_CLOCK_MONOTONIC, &sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE uint64_t get_now_monotonic_u64(void) {
	struct timespec ts;
	assert(!clock_gettime(X_CLOCK_MONOTONIC, &ts));
	return (uint64_t)(ts.tv_nsec / 1000) + (uint64_t)ts.tv_sec * 1000000;
}

INLINE uint64_t get_now_id(void) {
	uint64_t now = get_now_monotonic_u64();
	return (uint64_t)triple_u32(now) | ((uint64_t)triple_u32(now + 12345) << 32);
}

#undef X_CLOCK_MONOTONIC

INLINE long double get_now_real(void) {
	time_t sec;
	long msec;
	get_now(CLOCK_REALTIME, &sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE unsigned get_cores_available(void) {
	long cores_sysconf = sysconf(_SC_NPROCESSORS_ONLN);
	cores_sysconf = (cores_sysconf < 0 ? 0 : cores_sysconf);
	return max_u(min_u(cores_sysconf, 4), 1);
}

INLINE int flock_timedwait_monotonic(int fd, long double timeout) {
	long double deadline_ts = get_now_monotonic() + timeout;
	int retval = -1;

	while (true) {
		retval = flock(fd, LOCK_EX | LOCK_NB);
		if (retval == 0 || errno != EWOULDBLOCK || get_now_monotonic() > deadline_ts) {
			break;
		}
		if (usleep(1000) < 0) {
			break;
		}
	}
	return retval;
}

INLINE char *errno_to_string(int error, char *buf, size_t size) {
	assert(buf);
	assert(size > 0);
	locale_t locale = newlocale(LC_MESSAGES_MASK, "C", NULL);
	char *str = "!!! newlocale() error !!!";
	strncpy(buf, (locale ? strerror_l(error, locale) : str), size - 1);
	buf[size - 1] = '\0';
	if (locale) {
		freelocale(locale);
	}
	return buf;
}
