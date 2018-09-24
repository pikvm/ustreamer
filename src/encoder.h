/*****************************************************************************
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

#include "tools.h"
#include "device.h"

#ifdef OMX_ENCODER
#	include "omx/encoder.h"
#	define ENCODER_TYPES_OMX_HINT ", OMX"
#else
#	define ENCODER_TYPES_OMX_HINT ""
#endif


#define ENCODER_TYPES_STR \
	"CPU" \
	ENCODER_TYPES_OMX_HINT

enum encoder_type_t {
	ENCODER_TYPE_UNKNOWN, // Only for encoder_parse_type() and main()
	ENCODER_TYPE_CPU,

#ifdef OMX_ENCODER
	ENCODER_TYPE_OMX,
#endif
};

struct encoder_t {
	enum encoder_type_t type;

#ifdef OMX_ENCODER
	struct omx_encoder_t *omx;
#endif
};


struct encoder_t *encoder_init();
void encoder_destroy(struct encoder_t *encoder);

enum encoder_type_t encoder_parse_type(const char *const str);

void encoder_prepare(struct encoder_t *encoder);
void encoder_prepare_for_device(struct encoder_t *encoder, struct device_t *dev);
int encoder_compress_buffer(struct encoder_t *encoder, struct device_t *dev, int index);
