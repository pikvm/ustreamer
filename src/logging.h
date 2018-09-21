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
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

#include "tools.h"


unsigned log_level;
pthread_mutex_t log_mutex;


#define LOG_LEVEL_INFO		0
#define LOG_LEVEL_VERBOSE	1
#define LOG_LEVEL_PERF		2
#define LOG_LEVEL_DEBUG		3


#define LOGGING_INIT	assert(!pthread_mutex_init(&log_mutex, NULL))
#define LOGGING_DESTROY	assert(!pthread_mutex_destroy(&log_mutex))

#define LOGGING_LOCK	assert(!pthread_mutex_lock(&log_mutex))
#define LOGGING_UNLOCK	assert(!pthread_mutex_unlock(&log_mutex))


#define SEP_INFO(_x_ch) { \
		LOGGING_LOCK; \
		for (int _i = 0; _i < 80; ++_i) { \
			putchar(_x_ch); \
		} \
		putchar('\n'); \
		LOGGING_UNLOCK; \
	}

#define SEP_DEBUG(_x_ch) { \
		if (log_level >= LOG_LEVEL_DEBUG) { \
			SEP_INFO(_x_ch); \
		} \
	}

#define LOG_ERROR(_x_msg, ...) { \
		LOGGING_LOCK; \
		printf("-- ERROR [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); \
		LOGGING_UNLOCK; \
	}

#define LOG_PERROR(_x_msg, ...) { \
		char _buf[1024]; \
		strerror_r(errno, _buf, 1024); \
		LOGGING_LOCK; \
		printf("-- ERROR [%.03Lf tid=%ld] -- " _x_msg ": %s\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__, _buf); \
		LOGGING_UNLOCK; \
	}

#define LOG_INFO(_x_msg, ...) { \
		LOGGING_LOCK; \
		printf("-- INFO  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); \
		LOGGING_UNLOCK; \
	}

#define LOG_INFO_NOLOCK(_x_msg, ...) { \
		printf("-- INFO  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); \
	}

#define LOG_VERBOSE(_x_msg, ...) { \
		if (log_level >= LOG_LEVEL_VERBOSE) { \
			LOGGING_LOCK; \
			printf("-- VERB  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); \
			LOGGING_UNLOCK; \
		} \
	}

#define LOG_PERF(_x_msg, ...) { \
		if (log_level >= LOG_LEVEL_PERF) { \
			LOGGING_LOCK; \
			printf("-- PERF  [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); \
			LOGGING_UNLOCK; \
		} \
	}

#define LOG_DEBUG(_x_msg, ...) { \
		if (log_level >= LOG_LEVEL_DEBUG) { \
			LOGGING_LOCK; \
			printf("-- DEBUG [%.03Lf tid=%ld] -- " _x_msg "\n", now_ms_ld(), syscall(SYS_gettid), ##__VA_ARGS__); \
			LOGGING_UNLOCK; \
		} \
	}
