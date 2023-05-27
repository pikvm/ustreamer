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


#include <janus/plugins/plugin.h>

#include "uslibs/tools.h"

#include "const.h"


#define US_JLOG_INFO(x_prefix, x_msg, ...)	JANUS_LOG(LOG_INFO, "== %s/%-9s -- " x_msg "\n", US_PLUGIN_NAME, x_prefix, ##__VA_ARGS__)
#define US_JLOG_WARN(x_prefix, x_msg, ...)	JANUS_LOG(LOG_WARN, "== %s/%-9s -- " x_msg "\n", US_PLUGIN_NAME, x_prefix, ##__VA_ARGS__)
#define US_JLOG_ERROR(x_prefix, x_msg, ...)	JANUS_LOG(LOG_ERR, "== %s/%-9s -- " x_msg "\n", US_PLUGIN_NAME, x_prefix, ##__VA_ARGS__)

#define US_JLOG_PERROR(x_prefix, x_msg, ...) { \
		char *const m_perror_str = us_errno_to_string(errno); \
		JANUS_LOG(LOG_ERR, "[%s/%-9s] " x_msg ": %s\n", US_PLUGIN_NAME, x_prefix, ##__VA_ARGS__, m_perror_str); \
		free(m_perror_str); \
	}

#define US_ONCE(...) { \
		const int m_reported = __LINE__; \
		if (m_reported != once) { \
			__VA_ARGS__; \
			once = m_reported; \
		} \
	}
