/*****************************************************************************
#                                                                            #
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

#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>
#include <linux/videodev2.h>

#include "../libs/common/tools.h"
#include "../libs/common/threading.h"
#include "../libs/common/logging.h"
#include "../libs/common/frame.h"

#include "blank.h"
#include "device.h"
#include "encoder.h"
#include "workers.h"
#ifdef WITH_OMX
#	include "h264/encoder.h"
#	include "../libs/memsink/memsink.h"
#endif
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


typedef struct {
	atomic_bool stop;
	atomic_bool slowdown;
} process_s;

typedef struct {
	frame_s			*frame;
	unsigned		captured_fps;
	atomic_bool		updated;
	long double		last_as_blank_ts;
	pthread_mutex_t	mutex;
} video_s;

#ifdef WITH_OMX
typedef struct {
	h264_encoder_s	*encoder;
	frame_s			*dest;
	memsink_s		*sink;
} h264_stream_s;
#endif

typedef struct {
	int			last_as_blank;
	unsigned	error_delay;

	device_s	*dev;
	encoder_s	*encoder;
	frame_s		*blank;
#	ifdef WITH_OMX
	memsink_s	*h264_sink;
#	endif

	process_s		*proc;
	video_s			*video;
#	ifdef WITH_OMX
	h264_stream_s	*h264;
#	endif
} stream_s;


stream_s *stream_init(device_s *dev, encoder_s *encoder);
void stream_destroy(stream_s *stream);

void stream_loop(stream_s *stream);
void stream_loop_break(stream_s *stream);
void stream_switch_slowdown(stream_s *stream, bool slowdown);
