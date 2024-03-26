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

#include <stdatomic.h>

#include <pthread.h>

#include "../libs/types.h"
#include "../libs/queue.h"
#include "../libs/ring.h"
#include "../libs/memsink.h"
#include "../libs/capture.h"
#ifdef WITH_V4P
#	include "../libs/drm/drm.h"
#endif

#include "blank.h"
#include "encoder.h"
#include "h264.h"


typedef struct {
	us_h264_stream_s *h264;

#	ifdef WITH_V4P
	us_drm_s		*drm;
	int				drm_opened;
	ldf				drm_blank_at_ts;
#	endif

	us_ring_s		*http_jpeg_ring;
	atomic_bool		http_has_clients;
	atomic_uint		http_snapshot_requested;
	atomic_ullong	http_last_request_ts; // Seconds
	atomic_ullong	http_capture_state; // Bits

	us_blank_s		*blank;

	atomic_bool		stop;
} us_stream_runtime_s;

typedef struct {
	us_capture_s	*cap;
	us_encoder_s	*enc;

	int				last_as_blank;
	bool			slowdown;
	uint			error_delay;
	uint			exit_on_no_clients;

	us_memsink_s	*jpeg_sink;
	us_memsink_s	*raw_sink;

	us_memsink_s	*h264_sink;
	uint			h264_bitrate;
	uint			h264_gop;
	char			*h264_m2m_path;

#	ifdef WITH_V4P
	bool			v4p;
#	endif

	us_stream_runtime_s	*run;
} us_stream_s;


us_stream_s *us_stream_init(us_capture_s *cap, us_encoder_s *enc);
void us_stream_destroy(us_stream_s *stream);

void us_stream_loop(us_stream_s *stream);
void us_stream_loop_break(us_stream_s *stream);

void us_stream_get_capture_state(us_stream_s *stream, uint *width, uint *height, bool *online, uint *captured_fps);
