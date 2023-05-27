/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <limits.h>
#include <locale.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/file.h>

#if defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 32
#	define HAS_SIGABBREV_NP
#else
#	include <signal.h>
#endif


#ifdef NDEBUG
#	error WTF dude? Asserts are good things!
#endif

#if CHAR_BIT != 8
#	error There are not 8 bits in a char!
#endif


#define RN "\r\n"

#define INLINE inline __attribute__((always_inline))
#define UNUSED __attribute__((unused))

#define US_CALLOC(x_dest, x_nmemb)		assert(((x_dest) = calloc((x_nmemb), sizeof(*(x_dest)))) != NULL)
#define US_REALLOC(x_dest, x_nmemb)		assert(((x_dest) = realloc((x_dest), (x_nmemb) * sizeof(*(x_dest)))) != NULL)
#define US_DELETE(x_dest, x_free)		{ if (x_dest) { x_free(x_dest); } }
#define US_MEMSET_ZERO(x_obj)			memset(&(x_obj), 0, sizeof(x_obj))

#define US_ASPRINTF(x_dest, x_fmt, ...) assert(asprintf(&(x_dest), (x_fmt), ##__VA_ARGS__) >= 0)


INLINE char *us_strdup(const char *str) {
	char *const new = strdup(str);
	assert(new != NULL);
	return new;
}

INLINE const char *us_bool_to_string(bool flag) {
	return (flag ? "true" : "false");
}

INLINE size_t us_align_size(size_t size, size_t to) {
	return ((size + (to - 1)) & ~(to - 1));
}

INLINE unsigned us_min_u(unsigned a, unsigned b) {
	return (a < b ? a : b);
}

INLINE unsigned us_max_u(unsigned a, unsigned b) {
	return (a > b ? a : b);
}

INLINE long long us_floor_ms(long double now) {
	return (long long)now - (now < (long long)now); // floor()
}

INLINE uint32_t us_triple_u32(uint32_t x) {
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

INLINE void us_get_now(clockid_t clk_id, time_t *sec, long *msec) {
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
#	define _X_CLOCK_MONOTONIC CLOCK_MONOTONIC_RAW
#elif defined(CLOCK_MONOTONIC_FAST)
#	define _X_CLOCK_MONOTONIC CLOCK_MONOTONIC_FAST
#else
#	define _X_CLOCK_MONOTONIC CLOCK_MONOTONIC
#endif

INLINE long double us_get_now_monotonic(void) {
	time_t sec;
	long msec;
	us_get_now(_X_CLOCK_MONOTONIC, &sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE uint64_t us_get_now_monotonic_u64(void) {
	struct timespec ts;
	assert(!clock_gettime(_X_CLOCK_MONOTONIC, &ts));
	return (uint64_t)(ts.tv_nsec / 1000) + (uint64_t)ts.tv_sec * 1000000;
}

#undef _X_CLOCK_MONOTONIC

INLINE uint64_t us_get_now_id(void) {
	const uint64_t now = us_get_now_monotonic_u64();
	return (uint64_t)us_triple_u32(now) | ((uint64_t)us_triple_u32(now + 12345) << 32);
}

INLINE long double us_get_now_real(void) {
	time_t sec;
	long msec;
	us_get_now(CLOCK_REALTIME, &sec, &msec);
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE unsigned us_get_cores_available(void) {
	long cores_sysconf = sysconf(_SC_NPROCESSORS_ONLN);
	cores_sysconf = (cores_sysconf < 0 ? 0 : cores_sysconf);
	return us_max_u(us_min_u(cores_sysconf, 4), 1);
}

INLINE void us_ld_to_timespec(long double ld, struct timespec *ts) {
	ts->tv_sec = (long)ld;
	ts->tv_nsec = (ld - ts->tv_sec) * 1000000000L;
	if (ts->tv_nsec > 999999999L) {
		ts->tv_sec += 1;
		ts->tv_nsec = 0;
	}
}

INLINE long double us_timespec_to_ld(const struct timespec *ts) {
	return ts->tv_sec + ((long double)ts->tv_nsec) / 1000000000;
}

INLINE int us_flock_timedwait_monotonic(int fd, long double timeout) {
	const long double deadline_ts = us_get_now_monotonic() + timeout;
	int retval = -1;

	while (true) {
		retval = flock(fd, LOCK_EX | LOCK_NB);
		if (retval == 0 || errno != EWOULDBLOCK || us_get_now_monotonic() > deadline_ts) {
			break;
		}
		if (usleep(1000) < 0) {
			break;
		}
	}
	return retval;
}

INLINE char *us_errno_to_string(int error) {
	locale_t locale = newlocale(LC_MESSAGES_MASK, "C", NULL);
	char *buf;
	if (locale) {
		buf = us_strdup(strerror_l(error, locale));
		freelocale(locale);
	} else {
		buf = us_strdup("!!! newlocale() error !!!");
	}
	return buf;
}

INLINE char *us_signum_to_string(int signum) {
#	ifdef HAS_SIGABBREV_NP
	const char *const name = sigabbrev_np(signum);
#	else
	const char *const name = (
		signum == SIGTERM ? "TERM" :
		signum == SIGINT ? "INT" :
		signum == SIGPIPE ? "PIPE" :
		NULL
	);
#	endif
	char *buf;
	if (name != NULL) {
		US_ASPRINTF(buf, "SIG%s", name);
	} else {
		US_ASPRINTF(buf, "SIG[%d]", signum);
	}
	return buf;
}
