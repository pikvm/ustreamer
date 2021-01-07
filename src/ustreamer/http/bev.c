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


#include "bev.h"


char *bufferevent_my_format_reason(short what) {
	char *reason;
	A_CALLOC(reason, 2048);

	char perror_buf[1024] = {0};
	char *perror_ptr = errno_to_string(EVUTIL_SOCKET_ERROR(), perror_buf, 1024); // evutil_socket_error_to_string() is not thread-safe
	bool first = true;

	strcat(reason, perror_ptr);
	strcat(reason, " (");

#	define FILL_REASON(_bev, _name) { \
			if (what & _bev) { \
				if (first) { \
					first = false; \
				} else { \
					strcat(reason, ","); \
				} \
				strcat(reason, _name); \
			} \
		}

	FILL_REASON(BEV_EVENT_READING, "reading");
	FILL_REASON(BEV_EVENT_WRITING, "writing");
	FILL_REASON(BEV_EVENT_ERROR, "error");
	FILL_REASON(BEV_EVENT_TIMEOUT, "timeout");
	FILL_REASON(BEV_EVENT_EOF, "eof"); // cppcheck-suppress unreadVariable

#	undef FILL_REASON

	strcat(reason, ")");
	return reason;
}
