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

#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>
#include <linux/videodev2.h>

#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"

#include "blank.h"
#include "device.h"
#include "encoder.h"
#include "workers.h"
#include "h264.h"
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


typedef struct {
	us_frame_s		*frame;
	unsigned		captured_fps;
	atomic_bool		updated;
	pthread_mutex_t	mutex;

	atomic_bool		has_clients; // For slowdown
} us_video_s;

typedef struct {
	us_video_s		*video;
	long double		last_as_blank_ts;

	us_h264_stream_s	*h264;

	atomic_bool		stop;
} us_stream_runtime_s;

typedef struct {
	us_device_s		*dev;
	us_encoder_s	*enc;

	us_frame_s		*blank;
	int				last_as_blank;
	bool			slowdown;
	unsigned		error_delay;

	us_memsink_s	*sink;
	us_memsink_s	*raw_sink;

	us_memsink_s	*h264_sink;
	unsigned		h264_bitrate;
	unsigned		h264_gop;
	char			*h264_m2m_path;

	us_stream_runtime_s	*run;
} us_stream_s;


us_stream_s *us_stream_init(us_device_s *dev, us_encoder_s *enc);
void us_stream_destroy(us_stream_s *stream);

void us_stream_loop(us_stream_s *stream);
void us_stream_loop_break(us_stream_s *stream);

bool us_stream_has_clients(us_stream_s *stream);
