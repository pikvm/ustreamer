/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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

#include <pthread.h>

#include "types.h"
#include "tools.h"
#include "threading.h"
#include "logging_base.h"


enum us_log_level_t {
    US_LOG_LEVEL_INFO,
    US_LOG_LEVEL_PERF,
    US_LOG_LEVEL_VERBOSE,
    US_LOG_LEVEL_DEBUG,
};


extern enum us_log_level_t us_g_log_level;

extern bool us_g_log_colored;

extern pthread_mutex_t us_g_log_mutex;


#define US_LOGGING_INIT { \
		us_g_log_level = US_LOG_LEVEL_INFO; \
		us_g_log_colored = isatty(2); \
		US_MUTEX_INIT(us_g_log_mutex); \
	}

#define US_LOGGING_DESTROY	US_MUTEX_DESTROY(us_g_log_mutex)

#define US_LOGGING_LOCK		US_MUTEX_LOCK(us_g_log_mutex)
#define US_LOGGING_UNLOCK	US_MUTEX_UNLOCK(us_g_log_mutex)


#define US_COLOR_GRAY		"\x1b[30;1m"
#define US_COLOR_RED		"\x1b[31;1m"
#define US_COLOR_GREEN		"\x1b[32;1m"
#define US_COLOR_YELLOW		"\x1b[33;1m"
#define US_COLOR_BLUE		"\x1b[34;1m"
#define US_COLOR_CYAN		"\x1b[36;1m"
#define US_COLOR_RESET		"\x1b[0m"


// XXX: See the definition of US_LOG_PRINTF_NOLOCK() in logging_base.h

#define US_LOG_PRINTF(x_label_color, x_label, x_msg_color, x_msg, ...) { \
		US_LOGGING_LOCK; \
		US_LOG_PRINTF_NOLOCK(x_label_color, x_label, x_msg_color, x_msg, ##__VA_ARGS__); \
		US_LOGGING_UNLOCK; \
	}

#define US_LOG_SEP_INFO { \
		US_LOG_PRINTF(US_COLOR_GREEN, "INFO ", US_COLOR_GREEN, "============================================="); \
	}

#define US_LOG_ERROR(x_msg, ...) { \
		US_LOG_PRINTF(US_COLOR_RED, "ERROR", US_COLOR_RED, x_msg, ##__VA_ARGS__); \
	}

#define US_LOG_PERROR(x_msg, ...) { \
		char *const m_perror_str = us_errno_to_string(errno); \
		US_LOG_ERROR(x_msg ": %s", ##__VA_ARGS__, m_perror_str); \
		free(m_perror_str); \
	}

// We don't include alsa, speex and opus headers here
#define US_LOG_PERROR_ALSA(x_err, x_msg, ...)	US_LOG_ERROR(x_msg ": %s", ##__VA_ARGS__, snd_strerror(x_err))
#define US_LOG_PERROR_RES(x_err, x_msg, ...)	US_LOG_ERROR(x_msg ": %s", ##__VA_ARGS__, speex_resampler_strerror(x_err))
#define US_LOG_PERROR_OPUS(x_err, x_msg, ...)	US_LOG_ERROR(x_msg ": %s", ##__VA_ARGS__, opus_strerror(x_err))

#define US_LOG_INFO(x_msg, ...) { \
		US_LOG_PRINTF(US_COLOR_GREEN, "INFO ", "", x_msg, ##__VA_ARGS__); \
	}

#define US_LOG_INFO_NOLOCK(x_msg, ...) { \
		US_LOG_PRINTF_NOLOCK(US_COLOR_GREEN, "INFO ", "", x_msg, ##__VA_ARGS__); \
	}

#define US_LOG_PERF(x_msg, ...) { \
		if (us_g_log_level >= US_LOG_LEVEL_PERF) { \
			US_LOG_PRINTF(US_COLOR_CYAN, "PERF ", US_COLOR_CYAN, x_msg, ##__VA_ARGS__); \
		} \
	}

#define US_LOG_PERF_FPS(x_msg, ...) { \
		if (us_g_log_level >= US_LOG_LEVEL_PERF) { \
			US_LOG_PRINTF(US_COLOR_YELLOW, "PERF ", US_COLOR_YELLOW, x_msg, ##__VA_ARGS__); \
		} \
	}

#define US_LOG_VERBOSE(x_msg, ...) { \
		if (us_g_log_level >= US_LOG_LEVEL_VERBOSE) { \
			US_LOG_PRINTF(US_COLOR_BLUE, "VERB ", US_COLOR_BLUE, x_msg, ##__VA_ARGS__); \
		} \
	}

#define US_LOG_VERBOSE_PERROR(x_msg, ...) { \
		if (us_g_log_level >= US_LOG_LEVEL_VERBOSE) { \
			char *m_perror_str = us_errno_to_string(errno); \
			US_LOG_PRINTF(US_COLOR_BLUE, "VERB ", US_COLOR_BLUE, x_msg ": %s", ##__VA_ARGS__, m_perror_str); \
			free(m_perror_str); \
		} \
	}

#define US_LOG_DEBUG(x_msg, ...) { \
		if (us_g_log_level >= US_LOG_LEVEL_DEBUG) { \
			US_LOG_PRINTF(US_COLOR_GRAY, "DEBUG", US_COLOR_GRAY, x_msg, ##__VA_ARGS__); \
		} \
	}
