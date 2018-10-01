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
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/syscall.h>

#include "tools.h"


unsigned log_level;
pthread_mutex_t log_mutex;


#define LOG_LEVEL_INFO		0
#define LOG_LEVEL_PERF		1
#define LOG_LEVEL_VERBOSE	2
#define LOG_LEVEL_DEBUG		3


#define LOGGING_INIT	assert(!pthread_mutex_init(&log_mutex, NULL))
#define LOGGING_DESTROY	assert(!pthread_mutex_destroy(&log_mutex))

#define LOGGING_LOCK	assert(!pthread_mutex_lock(&log_mutex))
#define LOGGING_UNLOCK	assert(!pthread_mutex_unlock(&log_mutex))


#define SEP_INFO(_ch) { \
		LOGGING_LOCK; \
		for (int _i = 0; _i < 80; ++_i) { \
			putchar(_ch); \
		} \
		putchar('\n'); \
		fflush(stdout); \
		LOGGING_UNLOCK; \
	}

#define SEP_DEBUG(_ch) { \
		if (log_level >= LOG_LEVEL_DEBUG) { \
			SEP_INFO(_ch); \
		} \
	}

#define LOG_PRINTF_NOLOCK(_label, _msg, ...) { \
		printf("-- " _label " [%.03Lf tid=%ld] -- " _msg "\n", now_monotonic_ms(), syscall(SYS_gettid), ##__VA_ARGS__); \
		fflush(stdout); \
	}

#define LOG_ERROR(_msg, ...) { \
		LOGGING_LOCK; \
		LOG_PRINTF_NOLOCK("ERROR", _msg, ##__VA_ARGS__); \
		LOGGING_UNLOCK; \
	}

#define LOG_PERROR(_msg, ...) { \
		char _buf[1024] = ""; \
		strerror_r(errno, _buf, 1024); \
		LOGGING_LOCK; \
		printf("-- ERROR [%.03Lf tid=%ld] -- " _msg ": %s\n", now_monotonic_ms(), syscall(SYS_gettid), ##__VA_ARGS__, _buf); \
		fflush(stdout); \
		LOGGING_UNLOCK; \
	}

#define LOG_INFO(_msg, ...) { \
		LOGGING_LOCK; \
		LOG_PRINTF_NOLOCK("INFO ", _msg, ##__VA_ARGS__); \
		LOGGING_UNLOCK; \
	}

#define LOG_INFO_NOLOCK(_msg, ...) { \
		LOG_PRINTF_NOLOCK("INFO ", _msg, ##__VA_ARGS__); \
	}

#define LOG_PERF(_msg, ...) { \
		if (log_level >= LOG_LEVEL_PERF) { \
			LOGGING_LOCK; \
			LOG_PRINTF_NOLOCK("PERF ", _msg, ##__VA_ARGS__); \
			LOGGING_UNLOCK; \
		} \
	}

#define LOG_VERBOSE(_msg, ...) { \
		if (log_level >= LOG_LEVEL_VERBOSE) { \
			LOGGING_LOCK; \
			LOG_PRINTF_NOLOCK("VERB ", _msg, ##__VA_ARGS__); \
			LOGGING_UNLOCK; \
		} \
	}

#define LOG_DEBUG(_msg, ...) { \
		if (log_level >= LOG_LEVEL_DEBUG) { \
			LOGGING_LOCK; \
			LOG_PRINTF_NOLOCK("DEBUG", _msg, ##__VA_ARGS__); \
			LOGGING_UNLOCK; \
		} \
	}
