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
#include <unistd.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/syscall.h>

#include <pthread.h>
#ifdef WITH_PTHREAD_NP
#	if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#		include <pthread_np.h>
#		include <sys/param.h>
#	endif
#endif

#include "tools.h"


#ifdef PTHREAD_MAX_NAMELEN_NP
#	define MAX_THREAD_NAME ((size_t)(PTHREAD_MAX_NAMELEN_NP))
#else
#	define MAX_THREAD_NAME ((size_t)16)
#endif

#define A_THREAD_CREATE(_tid, _func, _arg)	assert(!pthread_create(_tid, NULL, _func, _arg))
#define A_THREAD_JOIN(_tid)					assert(!pthread_join(_tid, NULL))

#ifdef WITH_PTHREAD_NP
#	define A_THREAD_RENAME(_fmt, ...) { \
			char _new_tname_buf[MAX_THREAD_NAME] = {0}; \
			assert(snprintf(_new_tname_buf, MAX_THREAD_NAME, _fmt, ##__VA_ARGS__) > 0); \
			thread_set_name(_new_tname_buf); \
		}
#else
#	define A_THREAD_RENAME(_fmt, ...)
#endif

#define A_MUTEX_INIT(_mutex)	assert(!pthread_mutex_init(_mutex, NULL))
#define A_MUTEX_DESTROY(_mutex)	assert(!pthread_mutex_destroy(_mutex))
#define A_MUTEX_LOCK(_mutex)	assert(!pthread_mutex_lock(_mutex))
#define A_MUTEX_UNLOCK(_mutex)	assert(!pthread_mutex_unlock(_mutex))

#define A_COND_INIT(_cond)		assert(!pthread_cond_init(_cond, NULL))
#define A_COND_DESTROY(_cond)	assert(!pthread_cond_destroy(_cond))
#define A_COND_SIGNAL(...)		assert(!pthread_cond_signal(__VA_ARGS__))
#define A_COND_WAIT_TRUE(_var, _cond, _mutex) { while(!_var) assert(!pthread_cond_wait(_cond, _mutex)); }


#ifdef WITH_PTHREAD_NP
INLINE void thread_set_name(const char *name) {
#	if defined(__linux__)
	pthread_setname_np(pthread_self(), name);
#	elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
	pthread_set_name_np(pthread_self(), name);
#	elif defined(__NetBSD__)
	pthread_setname_np(pthread_self(), "%s", (void *)name);
#	else
#		error thread_set_name() not implemented, you can disable it using WITH_PTHREAD_NP=0
#	endif
}
#endif

INLINE void thread_get_name(char *name) { // Always required for logging
#ifdef WITH_PTHREAD_NP
	int retval = -1;
#	if defined(__linux__) || defined (__NetBSD__)
	retval = pthread_getname_np(pthread_self(), name, MAX_THREAD_NAME);
#	elif \
		(defined(__FreeBSD__) && defined(__FreeBSD_version) && __FreeBSD_version >= 1103500) \
		|| (defined(__OpenBSD__) && defined(OpenBSD) && OpenBSD >= 201905) \
		|| defined(__DragonFly__)
	pthread_get_name_np(pthread_self(), name, MAX_THREAD_NAME);
	if (name[0] != '\0') {
		retval = 0;
	}
#	else
#		error thread_get_name() not implemented, you can disable it using WITH_PTHREAD_NP=0
#	endif
	if (retval < 0) {
#endif

#if defined(__linux__)
		pid_t tid = syscall(SYS_gettid);
#elif defined(__FreeBSD__)
		pid_t tid = syscall(SYS_thr_self);
#elif defined(__OpenBSD__)
		pid_t tid = syscall(SYS_getthrid);
#elif defined(__NetBSD__)
		pid_t tid = syscall(SYS__lwp_self);
#elif defined(__DragonFly__)
		pid_t tid = syscall(SYS_lwp_gettid);
#else
		pid_t tid = 0; // Makes cppcheck happy
#	warning gettid() not implemented
#endif
		assert(snprintf(name, MAX_THREAD_NAME, "tid=%d", tid) > 0);

#ifdef WITH_PTHREAD_NP
	}
#endif
}
