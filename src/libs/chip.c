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


#include "chip.h"

#include <unistd.h>
#include <fcntl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include "types.h"
#include "errors.h"
#include "tools.h"
#include "xioctl.h"


#ifndef V4L2_CID_USER_TC358743_BASE
#	define V4L2_CID_USER_TC358743_BASE (V4L2_CID_USER_BASE + 0x1080)
#endif

#ifndef TC358743_CID_AUDIO_SAMPLING_RATE
#	define TC358743_CID_AUDIO_SAMPLING_RATE (V4L2_CID_USER_TC358743_BASE + 0)
#endif

#ifndef TC358743_CID_AUDIO_PRESENT
#	define TC358743_CID_AUDIO_PRESENT (V4L2_CID_USER_TC358743_BASE + 1)
#endif

#ifndef TC358743_CID_LANES_ENOUGH
#	define TC358743_CID_LANES_ENOUGH (V4L2_CID_USER_TC358743_BASE + 2)
#endif


int us_chip_check_cable(int fd) {
	struct v4l2_control ctl = {.id = V4L2_CID_DV_RX_POWER_PRESENT};
	if (us_xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) {
		return US_ERROR_COMMON;
	}
	return (ctl.value ? 0 : US_ERROR_NO_CABLE);
}

int us_chip_tc358743_check_lanes(int fd) {
	struct v4l2_control ctl = {.id = TC358743_CID_LANES_ENOUGH};
	if (us_xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) {
		return US_ERROR_COMMON;
	}
	return (ctl.value ? 0 : US_ERROR_NO_LANES);
}

int us_chip_tc358743_get_audio_hz(int fd) {
	struct v4l2_control ctl = {.id = TC358743_CID_AUDIO_PRESENT};
	if (us_xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) {
		return US_ERROR_COMMON;
	}
	if (!ctl.value) {
		return US_ERROR_NO_SIGNAL; // No audio
	}

	US_MEMSET_ZERO(ctl);
	ctl.id = TC358743_CID_AUDIO_SAMPLING_RATE;
	if (us_xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) {
		return US_ERROR_COMMON;
	}
	return (ctl.value ? ctl.value : US_ERROR_NO_SIGNAL);
}
