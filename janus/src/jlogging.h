/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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

#include "tools.h"


#define JLOG_INFO(_prefix, _msg, ...)	JANUS_LOG(LOG_INFO, "== ustreamer/%-9s -- " _msg "\n", _prefix, ##__VA_ARGS__)
#define JLOG_WARN(_prefix, _msg, ...)	JANUS_LOG(LOG_WARN, "== ustreamer/%-9s -- " _msg "\n", _prefix, ##__VA_ARGS__)
#define JLOG_ERROR(_prefix, _msg, ...)	JANUS_LOG(LOG_ERR, "== ustreamer/%-9s -- " _msg "\n", _prefix, ##__VA_ARGS__)

#define JLOG_PERROR(_prefix, _msg, ...) { \
		char _perror_buf[1024] = {0}; \
		char *_perror_ptr = errno_to_string(errno, _perror_buf, 1023); \
		JANUS_LOG(LOG_ERR, "[ustreamer/%-9s] " _msg ": %s\n", _prefix, ##__VA_ARGS__, _perror_ptr); \
	}
