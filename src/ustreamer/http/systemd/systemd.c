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


#include "systemd.h"

#include <unistd.h>
#include <assert.h>

#include <event2/http.h>
#include <event2/util.h>

#include <systemd/sd-daemon.h>

#include "../../../libs/tools.h"
#include "../../../libs/logging.h"


evutil_socket_t us_evhttp_bind_systemd(struct evhttp *http) {
	const int fds = sd_listen_fds(1);
	if (fds < 1) {
		US_LOG_ERROR("HTTP: No available systemd sockets");
		return -1;
	}

	int fd;
	for (fd = 1; fd < fds; ++fd) {
		close(SD_LISTEN_FDS_START + fd);
	}
	fd = SD_LISTEN_FDS_START;

	assert(!evutil_make_socket_nonblocking(fd));

	if (evhttp_accept_socket(http, fd) < 0) {
		US_LOG_PERROR("HTTP: Can't evhttp_accept_socket() systemd socket");
		return -1;
	}
	return fd;
}
