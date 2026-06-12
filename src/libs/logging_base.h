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

#include <pthread.h>

#include "types.h"
#include "tools.h"
#include "threading.h"


#define US_LOG_PRINTF_NOLOCK(x_label_color, x_label, x_msg_color, x_msg, ...) { \
		char m_tname_buf[US_THREAD_NAME_SIZE] = {0}; \
		us_thread_get_name(m_tname_buf); \
		if (us_g_log_colored) { \
			fprintf(stderr, US_COLOR_GRAY "-- " x_label_color x_label US_COLOR_GRAY \
				" [%.03Lf %9s]" " -- " US_COLOR_RESET x_msg_color x_msg US_COLOR_RESET, \
				us_get_now_monotonic(), m_tname_buf, ##__VA_ARGS__); \
		} else { \
			fprintf(stderr, "-- " x_label " [%.03Lf %9s] -- " x_msg, \
				us_get_now_monotonic(), m_tname_buf, ##__VA_ARGS__); \
		} \
		fputc('\n', stderr); \
		fflush(stderr); \
	}
