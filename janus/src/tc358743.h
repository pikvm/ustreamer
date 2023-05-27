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

#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/types.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include "uslibs/tools.h"
#include "uslibs/xioctl.h"

#include "logging.h"


typedef struct {
	bool		has_audio;
	unsigned	audio_hz;
} us_tc358743_info_s;


int us_tc358743_read_info(const char *path, us_tc358743_info_s *info);
