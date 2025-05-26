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


#pragma once

#include <linux/v4l2-controls.h>

#include "types.h"


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


int us_tc358743_xioctl_get_audio_hz(int fd, uint *audio_hz);
