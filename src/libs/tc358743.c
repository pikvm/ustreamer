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


#include "tc358743.h"

#include <unistd.h>
#include <fcntl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include "types.h"
#include "tools.h"
#include "xioctl.h"


int us_tc358743_xioctl_get_audio_hz(int fd, uint *audio_hz) {
	*audio_hz = 0;

	struct v4l2_control ctl = {.id = TC358743_CID_AUDIO_PRESENT};
	if (us_xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) {
		return -1;
	}
	if (!ctl.value) {
		return 0; // No audio
	}

	US_MEMSET_ZERO(ctl);
	ctl.id = TC358743_CID_AUDIO_SAMPLING_RATE;
	if (us_xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) {
		return -1;
	}
	*audio_hz = ctl.value;
	return 0;
}
