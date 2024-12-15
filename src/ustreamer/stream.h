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
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/capture.h"
#include "../libs/fpsi.h"
#ifdef WITH_V4P
#	include "../libs/drm/drm.h"
#endif

#include "blank.h"
#include "encoder.h"
#include "m2m.h"


typedef struct {
#	ifdef WITH_V4P
	atomic_bool		drm_live;
	us_fpsi_s		*drm_fpsi;
#	endif

	atomic_bool		h264_online;
	us_fpsi_s		*h264_fpsi;

	us_ring_s		*jpeg_ring;
	atomic_bool		has_clients;
	atomic_uint		snapshot_requested;
	atomic_ullong	last_request_ts; // Seconds
	us_fpsi_s		*captured_fpsi;
} us_stream_http_s;

typedef struct {
	us_stream_http_s	*http;

	us_m2m_encoder_s	*h264_enc;
	us_frame_s			*h264_tmp_src;
	us_frame_s			*h264_dest;
	bool				h264_key_requested;

	us_blank_s			*blank;

	us_fpsi_meta_s		notify_meta;

	atomic_bool			stop;
} us_stream_runtime_s;

typedef struct {
	us_capture_s	*cap;
	us_encoder_s	*enc;

	bool			notify_parent;
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
	us_drm_s		*drm;
#	endif

	us_stream_runtime_s	*run;
} us_stream_s;


us_stream_s *us_stream_init(us_capture_s *cap, us_encoder_s *enc);
void us_stream_update_blank(us_stream_s *stream, const us_capture_s *cap);
void us_stream_destroy(us_stream_s *stream);

void us_stream_loop(us_stream_s *stream);
void us_stream_loop_break(us_stream_s *stream);
