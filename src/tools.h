#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syscall.h>


bool log_debug;
bool log_perf;


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
	{ if (log_debug) { INNER_LOG_MK_PARAMS; \
	printf("-- DEBUG " INNER_LOG_PL " -- " _x_msg "\n", INNER_LOG_PARAMS, ##__VA_ARGS__); } }

#define LOG_PERF(_x_msg, ...) \
	{ if (log_debug || log_perf) { INNER_LOG_MK_PARAMS; \
	printf("-- PERF  " INNER_LOG_PL " -- " _x_msg "\n", INNER_LOG_PARAMS, ##__VA_ARGS__); } }

#define SEP_DEBUG(_x_ch) \
	{ if (log_debug) { SEP_INFO(_x_ch); } }

#define LOG_ERROR(_x_msg, ...) \
	{ INNER_LOG_MK_PARAMS; \
	printf("-- ERROR " INNER_LOG_PL " -- " _x_msg "\n", INNER_LOG_PARAMS, ##__VA_ARGS__); }

#define LOG_PERROR(_x_msg, ...) \
	{ INNER_LOG_MK_PARAMS; char _buf[1024]; strerror_r(errno, _buf, 1024); \
	printf("-- ERROR " INNER_LOG_PL " -- " _x_msg ": %s\n", INNER_LOG_PARAMS, ##__VA_ARGS__, _buf); }


#define A_PTHREAD_CREATE(_tid, _func, _arg)	assert(!pthread_create(_tid, NULL, _func, _arg))
#define A_PTHREAD_JOIN(_tid)				assert(!pthread_join(_tid, NULL))

#define A_PTHREAD_M_INIT(_mutex)	assert(!pthread_mutex_init(_mutex, NULL))
#define A_PTHREAD_M_DESTROY(_mutex)	assert(!pthread_mutex_destroy(_mutex))
#define A_PTHREAD_M_LOCK(...)		assert(!pthread_mutex_lock(__VA_ARGS__))
#define A_PTHREAD_M_UNLOCK(...)		assert(!pthread_mutex_unlock(__VA_ARGS__))

#define A_PTHREAD_C_INIT(_cond)		assert(!pthread_cond_init(_cond, NULL))
#define A_PTHREAD_C_DESTROY(_cond)	assert(!pthread_cond_destroy(_cond))
#define A_PTHREAD_C_SIGNAL(...)		assert(!pthread_cond_signal(__VA_ARGS__))
#define A_PTHREAD_C_WAIT_TRUE(_var, _cond, _mutex) \
	{ while(!_var) assert(!pthread_cond_wait(_cond, _mutex)); }


#define A_CALLOC(_dest, _nmemb, _size)	assert((_dest = calloc(_nmemb, _size)))
#define A_MALLOC(_dest, _size)			assert((_dest = malloc(_size)));
#define MEMSET_ZERO(_x_obj)				memset(&(_x_obj), 0, sizeof(_x_obj))


#define INLINE inline __attribute__((always_inline))

#define XIOCTL_RETRIES 4


INLINE void now_ms(time_t *sec, long *msec) {
	struct timespec spec;

	assert(!clock_gettime(CLOCK_REALTIME, &spec));
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
