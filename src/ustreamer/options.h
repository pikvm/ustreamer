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

#include "../libs/types.h"
#include "../libs/memsink.h"
#include "../libs/capture.h"
#ifdef WITH_V4P
#	include "../libs/drm/drm.h"
#endif

#include "encoder.h"
#include "stream.h"
#include "http/server.h"


typedef struct {
	uint			argc;
	char			**argv;
	char			**argv_copy;
	us_memsink_s	*jpeg_sink;
	us_memsink_s	*raw_sink;
	us_memsink_s	*h264_sink;
#	ifdef WITH_V4P
	us_drm_s		*drm;
#	endif
} us_options_s;


us_options_s *us_options_init(uint argc, char *argv[]);
void us_options_destroy(us_options_s *options);

int us_options_parse(
	us_options_s *options,
	us_capture_s *cap,
	us_encoder_s *enc,
	us_stream_s *stream,
	us_server_s *server);
