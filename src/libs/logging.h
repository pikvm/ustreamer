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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <pthread.h>

#include "tools.h"
#include "threading.h"


enum log_level_t {
    LOG_LEVEL_INFO,
    LOG_LEVEL_PERF,
    LOG_LEVEL_VERBOSE,
    LOG_LEVEL_DEBUG,
};


extern enum log_level_t us_log_level;

extern bool us_log_colored;

extern pthread_mutex_t us_log_mutex;


#define LOGGING_INIT { \
		us_log_level = LOG_LEVEL_INFO; \
		us_log_colored = isatty(2); \
		A_MUTEX_INIT(&us_log_mutex); \
	}

#define LOGGING_DESTROY A_MUTEX_DESTROY(&us_log_mutex)

#define LOGGING_LOCK	A_MUTEX_LOCK(&us_log_mutex)
#define LOGGING_UNLOCK	A_MUTEX_UNLOCK(&us_log_mutex)


#define COLOR_GRAY		"\x1b[30;1m"
#define COLOR_RED		"\x1b[31;1m"
#define COLOR_GREEN		"\x1b[32;1m"
#define COLOR_YELLOW	"\x1b[33;1m"
#define COLOR_BLUE		"\x1b[34;1m"
#define COLOR_CYAN		"\x1b[36;1m"
#define COLOR_RESET		"\x1b[0m"


#define SEP_INFO(_ch) { \
		LOGGING_LOCK; \
		for (int _i = 0; _i < 80; ++_i) { \
			fputc(_ch, stderr); \
		} \
		fputc('\n', stderr); \
		fflush(stderr); \
		LOGGING_UNLOCK; \
	}

#define SEP_DEBUG(_ch) { \
		if (us_log_level >= LOG_LEVEL_DEBUG) { \
			SEP_INFO(_ch); \
		} \
	}


#define LOG_PRINTF_NOLOCK(_label_color, _label, _msg_color, _msg, ...) { \
		char _tname_buf[MAX_THREAD_NAME] = {0}; \
		thread_get_name(_tname_buf); \
		if (us_log_colored) { \
			fprintf(stderr, COLOR_GRAY "-- " _label_color _label COLOR_GRAY \
				" [%.03Lf %9s]" " -- " COLOR_RESET _msg_color _msg COLOR_RESET, \
				get_now_monotonic(), _tname_buf, ##__VA_ARGS__); \
		} else { \
			fprintf(stderr, "-- " _label " [%.03Lf %9s] -- " _msg, \
				get_now_monotonic(), _tname_buf, ##__VA_ARGS__); \
		} \
		fputc('\n', stderr); \
		fflush(stderr); \
	}

#define LOG_PRINTF(_label_color, _label, _msg_color, _msg, ...) { \
		LOGGING_LOCK; \
		LOG_PRINTF_NOLOCK(_label_color, _label, _msg_color, _msg, ##__VA_ARGS__); \
		LOGGING_UNLOCK; \
	}

#define LOG_ERROR(_msg, ...) { \
		LOG_PRINTF(COLOR_RED, "ERROR", COLOR_RED, _msg, ##__VA_ARGS__); \
	}

#define LOG_PERROR(_msg, ...) { \
		char _perror_buf[1024] = {0}; \
		char *_perror_ptr = errno_to_string(errno, _perror_buf, 1024); \
		LOG_ERROR(_msg ": %s", ##__VA_ARGS__, _perror_ptr); \
	}

#define LOG_INFO(_msg, ...) { \
		LOG_PRINTF(COLOR_GREEN, "INFO ", "", _msg, ##__VA_ARGS__); \
	}

#define LOG_INFO_NOLOCK(_msg, ...) { \
		LOG_PRINTF_NOLOCK(COLOR_GREEN, "INFO ", "", _msg, ##__VA_ARGS__); \
	}

#define LOG_PERF(_msg, ...) { \
		if (us_log_level >= LOG_LEVEL_PERF) { \
			LOG_PRINTF(COLOR_CYAN, "PERF ", COLOR_CYAN, _msg, ##__VA_ARGS__); \
		} \
	}

#define LOG_PERF_FPS(_msg, ...) { \
		if (us_log_level >= LOG_LEVEL_PERF) { \
			LOG_PRINTF(COLOR_YELLOW, "PERF ", COLOR_YELLOW, _msg, ##__VA_ARGS__); \
		} \
	}

#define LOG_VERBOSE(_msg, ...) { \
		if (us_log_level >= LOG_LEVEL_VERBOSE) { \
			LOG_PRINTF(COLOR_BLUE, "VERB ", COLOR_BLUE, _msg, ##__VA_ARGS__); \
		} \
	}

#define LOG_VERBOSE_PERROR(_msg, ...) { \
		if (us_log_level >= LOG_LEVEL_VERBOSE) { \
			char _perror_buf[1024] = {0}; \
			char *_perror_ptr = errno_to_string(errno, _perror_buf, 1023); \
			LOG_PRINTF(COLOR_BLUE, "VERB ", COLOR_BLUE, _msg ": %s", ##__VA_ARGS__, _perror_ptr); \
		} \
	}

#define LOG_DEBUG(_msg, ...) { \
		if (us_log_level >= LOG_LEVEL_DEBUG) { \
			LOG_PRINTF(COLOR_GRAY, "DEBUG", COLOR_GRAY, _msg, ##__VA_ARGS__); \
		} \
	}
