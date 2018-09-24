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

#include <errno.h>

#include <sys/ioctl.h>

#include "tools.h"
#include "logging.h"


#define XIOCTL_RETRIES 4


INLINE int xioctl(const int fd, const int request, void *arg) {
	int retries = XIOCTL_RETRIES;
	int retval = -1;

	do {
		retval = ioctl(fd, request, arg);
	} while (
		retval
		&& retries--
		&& (
			errno == EINTR
			|| errno == EAGAIN
			|| errno == ETIMEDOUT
		)
	);

	if (retval && retries <= 0) {
		LOG_PERROR("ioctl(%d) retried %d times; giving up", request, XIOCTL_RETRIES);
	}
	return retval;
}
