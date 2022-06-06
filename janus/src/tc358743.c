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


#include "tc358743.h"


#ifndef V4L2_CID_USER_TC358743_BASE
#	define V4L2_CID_USER_TC358743_BASE (V4L2_CID_USER_BASE + 0x1080)
#endif
#ifndef TC358743_CID_AUDIO_PRESENT
#	define TC358743_CID_AUDIO_PRESENT (V4L2_CID_USER_TC358743_BASE + 1)
#endif
#ifndef TC358743_CID_AUDIO_SAMPLING_RATE
#	define TC358743_CID_AUDIO_SAMPLING_RATE (V4L2_CID_USER_TC358743_BASE + 0)
#endif


int tc358743_read_info(const char *path, tc358743_info_s *info) {
	MEMSET_ZERO(*info);

	int fd = -1;
	if ((fd = open(path, O_RDWR)) < 0) {
		JLOG_PERROR("audio", "Can't open TC358743 V4L2 device");
		return -1;
	}

#	define READ_CID(_cid, _field) { \
			struct v4l2_control ctl = {0}; \
			ctl.id = _cid; \
			if (xioctl(fd, VIDIOC_G_CTRL, &ctl) < 0) { \
				JLOG_PERROR("audio", "Can't get value of " #_cid); \
				close(fd); \
				return -1; \
			} \
			info->_field = ctl.value; \
		}

	READ_CID(TC358743_CID_AUDIO_PRESENT,		has_audio);
	READ_CID(TC358743_CID_AUDIO_SAMPLING_RATE,	audio_hz);

#	undef READ_CID

	close(fd);
	return 0;
}
