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
#	define US_MAX_THREAD_NAME ((size_t)(PTHREAD_MAX_NAMELEN_NP))
#else
#	define US_MAX_THREAD_NAME ((size_t)16)
#endif

#define US_THREAD_CREATE(x_tid, x_func, x_arg)	assert(!pthread_create(&(x_tid), NULL, (x_func), (x_arg)))
#define US_THREAD_JOIN(x_tid)					assert(!pthread_join((x_tid), NULL))

#ifdef WITH_PTHREAD_NP
#	define US_THREAD_RENAME(x_fmt, ...) { \
			char m_new_tname_buf[US_MAX_THREAD_NAME] = {0}; \
			assert(snprintf(m_new_tname_buf, US_MAX_THREAD_NAME, (x_fmt), ##__VA_ARGS__) > 0); \
			us_thread_set_name(m_new_tname_buf); \
		}
#else
#	define US_THREAD_RENAME(_fmt, ...)
#endif

#define US_MUTEX_INIT(x_mutex)		assert(!pthread_mutex_init(&(x_mutex), NULL))
#define US_MUTEX_DESTROY(x_mutex)	assert(!pthread_mutex_destroy(&(x_mutex)))
#define US_MUTEX_LOCK(x_mutex)		assert(!pthread_mutex_lock(&(x_mutex)))
#define US_MUTEX_UNLOCK(x_mutex)	assert(!pthread_mutex_unlock(&(x_mutex)))

#define US_COND_INIT(x_cond)		assert(!pthread_cond_init(&(x_cond), NULL))
#define US_COND_DESTROY(x_cond)		assert(!pthread_cond_destroy(&(x_cond)))
#define US_COND_SIGNAL(x_cond)		assert(!pthread_cond_signal(&(x_cond)))
#define US_COND_BROADCAST(x_cond)	assert(!pthread_cond_broadcast(&(x_cond)))
#define US_COND_WAIT_FOR(x_var, x_cond, x_mutex) { while(!(x_var)) assert(!pthread_cond_wait(&(x_cond), &(x_mutex))); }


#ifdef WITH_PTHREAD_NP
INLINE void us_thread_set_name(const char *name) {
#	if defined(__linux__)
	pthread_setname_np(pthread_self(), name);
#	elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
	pthread_set_name_np(pthread_self(), name);
#	elif defined(__NetBSD__)
	pthread_setname_np(pthread_self(), "%s", (void *)name);
#	else
#		error us_thread_set_name() not implemented, you can disable it using WITH_PTHREAD_NP=0
#	endif
}
#endif

INLINE void us_thread_get_name(char *name) { // Always required for logging
#ifdef WITH_PTHREAD_NP
	int retval = -1;
#	if defined(__linux__) || defined (__NetBSD__)
	retval = pthread_getname_np(pthread_self(), name, US_MAX_THREAD_NAME);
#	elif \
		(defined(__FreeBSD__) && defined(__FreeBSD_version) && __FreeBSD_version >= 1103500) \
		|| (defined(__OpenBSD__) && defined(OpenBSD) && OpenBSD >= 201905) \
		|| defined(__DragonFly__)
	pthread_get_name_np(pthread_self(), name, US_MAX_THREAD_NAME);
	if (name[0] != '\0') {
		retval = 0;
	}
#	else
#		error us_thread_get_name() not implemented, you can disable it using WITH_PTHREAD_NP=0
#	endif
	if (retval < 0) {
#endif

#if defined(__linux__)
		const pid_t tid = syscall(SYS_gettid);
#elif defined(__FreeBSD__)
		const pid_t tid = syscall(SYS_thr_self);
#elif defined(__OpenBSD__)
		const pid_t tid = syscall(SYS_getthrid);
#elif defined(__NetBSD__)
		const pid_t tid = syscall(SYS__lwp_self);
#elif defined(__DragonFly__)
		const pid_t tid = syscall(SYS_lwp_gettid);
#else
		const pid_t tid = 0; // Makes cppcheck happy
#	warning gettid() not implemented
#endif
		assert(snprintf(name, US_MAX_THREAD_NAME, "tid=%d", tid) > 0);

#ifdef WITH_PTHREAD_NP
	}
#endif
}
