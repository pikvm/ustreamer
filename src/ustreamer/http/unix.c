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


#include "unix.h"


evutil_socket_t us_evhttp_bind_unix(struct evhttp *http, const char *path, bool rm, mode_t mode) {
	struct sockaddr_un addr = {0};

#	define MAX_SUN_PATH (sizeof(addr.sun_path) - 1)

	if (strlen(path) > MAX_SUN_PATH) {
		US_LOG_ERROR("UNIX socket path is too long; max=%zu", MAX_SUN_PATH);
		return -1;
	}

	strncpy(addr.sun_path, path, MAX_SUN_PATH);
	addr.sun_family = AF_UNIX;

#	undef MAX_SUN_PATH

	const evutil_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(fd >= 0);
	assert(!evutil_make_socket_nonblocking(fd));

	if (rm && unlink(path) < 0) {
		if (errno != ENOENT) {
			US_LOG_PERROR("Can't remove old UNIX socket '%s'", path);
			return -1;
		}
	}
	if (bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
		US_LOG_PERROR("Can't bind HTTP to UNIX socket '%s'", path);
		return -1;
	}
	if (mode && chmod(path, mode) < 0) {
		US_LOG_PERROR("Can't set permissions %o to UNIX socket '%s'", mode, path);
		return -1;
	}
	if (listen(fd, 128) < 0) {
		US_LOG_PERROR("Can't listen UNIX socket '%s'", path);
		return -1;
	}
	if (evhttp_accept_socket(http, fd) < 0) {
		US_LOG_PERROR("Can't evhttp_accept_socket() UNIX socket '%s'", path);
		return -1;
	}
	return fd;
}
