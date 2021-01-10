/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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
#ifdef WITH_OMX
#	include "../libs/unjpeg.h"
#	include "h264/encoder.h"
#endif
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


typedef struct {
	frame_s			*frame;
	unsigned		captured_fps;
	atomic_bool		updated;
	pthread_mutex_t	mutex;
} video_s;

#ifdef WITH_OMX
typedef struct {
	memsink_s		*sink;
	frame_s			*tmp_src;
	frame_s			*dest;
	h264_encoder_s	*enc;
} h264_stream_s;
#endif

typedef struct {
	video_s		*video;
	long double	last_as_blank_ts;

#	ifdef WITH_OMX
	h264_stream_s *h264;
#	endif

	atomic_bool stop;
	atomic_bool slowdown;
} stream_runtime_s;

typedef struct {
	device_s	*dev;
	encoder_s	*enc;

	int			last_as_blank;
	unsigned	error_delay;

	frame_s		*blank;

	memsink_s	*sink;

#	ifdef WITH_OMX
	memsink_s	*h264_sink;
	unsigned	h264_bitrate;
	unsigned	h264_gop;
#	endif

	stream_runtime_s *run;
} stream_s;


stream_s *stream_init(device_s *dev, encoder_s *enc);
void stream_destroy(stream_s *stream);

void stream_loop(stream_s *stream);
void stream_loop_break(stream_s *stream);
void stream_switch_slowdown(stream_s *stream, bool slowdown);
