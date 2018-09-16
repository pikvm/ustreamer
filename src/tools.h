#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syscall.h>


bool debug;


#define SEP_INFO(_x_ch) \
	{ for (int _i = 0; _i < 80; ++_i) putchar(_x_ch); putchar('\n'); }

#define INNER_LOG_MK_PARAMS \
	time_t _sec; long _msec; now_ms(&_sec, &_msec); pid_t _tid = syscall(SYS_gettid);

#define INNER_LOG_PARAMS _sec, _msec, _tid

#define INNER_LOG_PL "[%ld.%03ld tid=%d]"

#define LOG_INFO(_x_msg, ...) \
	{ INNER_LOG_MK_PARAMS; \
	printf("-- INFO  " INNER_LOG_PL " -- " _x_msg "\n", INNER_LOG_PARAMS, ##__VA_ARGS__); }

#define LOG_DEBUG(_x_msg, ...) \
	{ if (debug) { INNER_LOG_MK_PARAMS; \
	printf("-- DEBUG " INNER_LOG_PL " -- " _x_msg "\n", INNER_LOG_PARAMS, ##__VA_ARGS__); } }

#define SEP_DEBUG(_x_ch) \
	{ if (debug) { SEP_INFO(_x_ch); } }

#define LOG_ERROR(_x_msg, ...) \
	{ INNER_LOG_MK_PARAMS; \
	printf("-- ERROR " INNER_LOG_PL " -- " _x_msg "\n", INNER_LOG_PARAMS, ##__VA_ARGS__); }

#define LOG_PERROR(_x_msg, ...) \
	{ INNER_LOG_MK_PARAMS; char _buf[1024]; strerror_r(errno, _buf, 1024); \
	printf("-- ERROR " INNER_LOG_PL " -- " _x_msg ": %s\n", INNER_LOG_PARAMS, ##__VA_ARGS__, _buf); }


#define MEMSET_ZERO(_x_obj) memset(&(_x_obj), 0, sizeof(_x_obj))

#define INLINE inline __attribute__((always_inline))

#define XIOCTL_RETRIES 4


INLINE void now(struct timespec *spec) {
	assert(!clock_gettime(CLOCK_REALTIME, spec));
}

INLINE void now_ms(time_t *sec, long *msec) {
	struct timespec spec;

	now(&spec);
	*sec = spec.tv_sec;
	*msec = round(spec.tv_nsec / 1.0e6);

	if (*msec > 999) {
		*sec += 1;
		*msec = 0;
	}
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
