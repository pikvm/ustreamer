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

#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>

#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/unjpeg.h"
#include "m2m.h"


typedef struct {
	us_memsink_s		*sink;
	bool				key_requested;
	us_frame_s			*tmp_src;
	us_frame_s			*dest;
	us_m2m_encoder_s	*enc;
	atomic_bool			online;
} us_h264_stream_s;


us_h264_stream_s *us_h264_stream_init(us_memsink_s *sink, const char *path, unsigned bitrate, unsigned gop);
void us_h264_stream_destroy(us_h264_stream_s *h264);
void us_h264_stream_process(us_h264_stream_s *h264, const us_frame_s *frame, bool force_key);
