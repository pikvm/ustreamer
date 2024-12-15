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


#include "tools.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <event2/http.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>
#include <event2/bufferevent.h>

#include "../../libs/types.h"
#include "../../libs/tools.h"
#include "../../libs/logging.h"


evutil_socket_t us_evhttp_bind_unix(struct evhttp *http, const char *path, bool rm, mode_t mode) {
	struct sockaddr_un addr = {0};
	const uz max_sun_path = sizeof(addr.sun_path) - 1;

	if (strlen(path) > max_sun_path) {
		US_LOG_ERROR("HTTP: UNIX socket path is too long; max=%zu", max_sun_path);
		return -1;
	}

	strncpy(addr.sun_path, path, max_sun_path);
	addr.sun_family = AF_UNIX;

	const evutil_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(fd >= 0);
	assert(!evutil_make_socket_nonblocking(fd));

	if (rm && unlink(path) < 0) {
		if (errno != ENOENT) {
			US_LOG_PERROR("HTTP: Can't remove old UNIX socket '%s'", path);
			return -1;
		}
	}
	if (bind(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) < 0) {
		US_LOG_PERROR("HTTP: Can't bind HTTP to UNIX socket '%s'", path);
		return -1;
	}
	if (mode && chmod(path, mode) < 0) {
		US_LOG_PERROR("HTTP: Can't set permissions %o to UNIX socket '%s'", mode, path);
		return -1;
	}
	if (listen(fd, 128) < 0) {
		US_LOG_PERROR("HTTP: Can't listen UNIX socket '%s'", path);
		return -1;
	}
	if (evhttp_accept_socket(http, fd) < 0) {
		US_LOG_PERROR("HTTP: Can't evhttp_accept_socket() UNIX socket '%s'", path);
		return -1;
	}
	return fd;
}

const char *us_evhttp_get_header(struct evhttp_request *request, const char *key) {
	return evhttp_find_header(evhttp_request_get_input_headers(request), key);
}

char *us_evhttp_get_hostport(struct evhttp_request *request) {
	char *addr = NULL;
	unsigned short port = 0;
	struct evhttp_connection *conn = evhttp_request_get_connection(request);
	if (conn != NULL) {
		char *peer;
		evhttp_connection_get_peer(conn, &peer, &port);
		addr = us_strdup(peer);
	}

	const char *xff = us_evhttp_get_header(request, "X-Forwarded-For");
	if (xff != NULL) {
		US_DELETE(addr, free);
		assert((addr = strndup(xff, 1024)) != NULL);
		for (uint index = 0; addr[index]; ++index) {
			if (addr[index] == ',') {
				addr[index] = '\0';
				break;
			}
		}
	}

	if (addr == NULL) {
		addr = us_strdup("???");
	}

	char *hostport;
	US_ASPRINTF(hostport, "[%s]:%u", addr, port);
	free(addr);
	return hostport;
}

bool us_evkeyvalq_get_true(struct evkeyvalq *params, const char *key) {
	const char *value_str = evhttp_find_header(params, key);
	if (value_str != NULL) {
		if (
			value_str[0] == '1'
			|| !evutil_ascii_strcasecmp(value_str, "true")
			|| !evutil_ascii_strcasecmp(value_str, "yes")
		) {
			return true;
		}
	}
	return false;
}

char *us_evkeyvalq_get_string(struct evkeyvalq *params, const char *key) {
	const char *const value_str = evhttp_find_header(params, key);
	if (value_str != NULL) {
		return evhttp_encode_uri(value_str);
	}
	return NULL;
}

char *us_bufferevent_format_reason(short what) {
	char *reason;
	US_CALLOC(reason, 2048);

	// evutil_socket_error_to_string() is not thread-safe
	char *const perror_str = us_errno_to_string(EVUTIL_SOCKET_ERROR());
	bool first = true;

	strncat(reason, perror_str, 1023);
	free(perror_str);
	strcat(reason, " (");

#	define FILL_REASON(x_bev, x_name) { \
			if (what & x_bev) { \
				if (first) { \
					first = false; \
				} else { \
					strcat(reason, ","); \
				} \
				strcat(reason, x_name); \
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
