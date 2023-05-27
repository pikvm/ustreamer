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


#pragma once

#include <errno.h>

#include <sys/ioctl.h>


#ifndef US_CFG_XIOCTL_RETRIES
#	define US_CFG_XIOCTL_RETRIES 4
#endif
#define _XIOCTL_RETRIES ((unsigned)(US_CFG_XIOCTL_RETRIES))


INLINE int us_xioctl(int fd, int request, void *arg) {
	int retries = _XIOCTL_RETRIES;
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
	return retval;
}
