#pragma once

#include <stdio.h>
#include <stdlib.h>
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


unsigned log_level;


#define LOG_LEVEL_INFO		0
#define LOG_LEVEL_VERBOSE	1
#define LOG_LEVEL_PERF		2
#define LOG_LEVEL_DEBUG		3

#define SEP_INFO(_x_ch) \
	{ for (int _i = 0; _i < 80; ++_i) putchar(_x_ch); putchar('\n'); }

#define SEP_DEBUG(_x_ch) \
	{ if (log_level >= LOG_LEVEL_DEBUG) { SEP_INFO(_x_ch); } }

#define LOG_ERROR(_x_msg, ...) \
	printf("-- ERROR [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__)

#define LOG_PERROR(_x_msg, ...) \
	{ char _buf[1024]; strerror_r(errno, _buf, 1024); \
	printf("-- ERROR [%.03Lf tid=%ld] -- " _x_msg ": %s\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__, _buf); }

#define LOG_INFO(_x_msg, ...) \
	printf("-- INFO  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__)

#define LOG_VERBOSE(_x_msg, ...) \
	{ if (log_level >= LOG_LEVEL_VERBOSE) \
	printf("-- VERB  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); }

#define LOG_PERF(_x_msg, ...) \
	{ if (log_level >= LOG_LEVEL_PERF) \
	printf("-- PERF  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); }

#define LOG_DEBUG(_x_msg, ...) \
	{ if (log_level >= LOG_LEVEL_DEBUG) \
	printf("-- DEBUG [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); }


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
#define A_REALLOC(_dest, _size)			assert((_dest = realloc(_dest, _size)))
#define MEMSET_ZERO(_x_obj)				memset(&(_x_obj), 0, sizeof(_x_obj))
#define MEMSET_ZERO_PTR(_x_ptr)			memset(_x_ptr, 0, sizeof(*(_x_ptr)))


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
