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
#include <assert.h>

#include <linux/videodev2.h>

#include <interface/mmal/mmal.h>
#include <interface/mmal/mmal_format.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_component_wrapper.h>
#include <interface/mmal/util/mmal_util_params.h>

#include "../../libs/common/tools.h"
#include "../../libs/common/logging.h"
#include "../../libs/common/frame.h"
#include "../../libs/common/unjpeg.h"


typedef struct {
	MMAL_WRAPPER_T		*wrapper;
	MMAL_PORT_T			*input_port;
	MMAL_PORT_T			*output_port;
	VCOS_SEMAPHORE_T	handler_sem;
	bool				i_handler_sem;

	frame_s		*tmp;
	int			last_online;

	unsigned width;
	unsigned height;
	unsigned format;
} h264_encoder_runtime_s;

typedef struct {
	unsigned gop; // Interval between keyframes
	unsigned bps; // Bit-per-sec
	unsigned fps;

	h264_encoder_runtime_s *run;
} h264_encoder_s;


h264_encoder_s *h264_encoder_init(void);
void h264_encoder_destroy(h264_encoder_s *enc);

int h264_encoder_prepare(h264_encoder_s *enc, unsigned width, unsigned height, unsigned format);
int h264_encoder_compress(h264_encoder_s *enc, const frame_s *src, frame_s *dest);
