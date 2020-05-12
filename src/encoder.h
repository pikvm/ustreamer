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

#include <stdbool.h>

#include <pthread.h>

#include "device.h"

#ifdef WITH_OMX
#	include "encoders/omx/encoder.h"

#	define ENCODER_TYPES_OMX_HINT ", OMX"

#	ifndef CFG_MAX_GLITCHED_RESOLUTIONS
#		define CFG_MAX_GLITCHED_RESOLUTIONS 1024
#	endif
#	define MAX_GLITCHED_RESOLUTIONS ((unsigned)(CFG_MAX_GLITCHED_RESOLUTIONS))
#else
#	define ENCODER_TYPES_OMX_HINT ""
#endif


#define ENCODER_TYPES_STR \
	"CPU, HW" \
	ENCODER_TYPES_OMX_HINT

enum encoder_type_t {
	ENCODER_TYPE_UNKNOWN, // Only for encoder_parse_type() and main()
	ENCODER_TYPE_CPU,
	ENCODER_TYPE_HW,
#	ifdef WITH_OMX
	ENCODER_TYPE_OMX,
#	endif
};

struct encoder_runtime_t {
	enum encoder_type_t	type;
	unsigned			quality;
	bool				cpu_forced;
	pthread_mutex_t		mutex;

#	ifdef WITH_OMX
	unsigned				n_omxs;
	struct omx_encoder_t	**omxs;
#	endif
};

struct encoder_t {
	enum encoder_type_t	type;
	unsigned			quality;
#	ifdef WITH_OMX
	unsigned	n_glitched_resolutions;
	unsigned	glitched_resolutions[2][MAX_GLITCHED_RESOLUTIONS];
#	endif

	struct encoder_runtime_t *run;
};


struct encoder_t *encoder_init(void);
void encoder_destroy(struct encoder_t *encoder);

enum encoder_type_t encoder_parse_type(const char *str);
const char *encoder_type_to_string(enum encoder_type_t type);

void encoder_prepare(struct encoder_t *encoder, struct device_t *dev);
int encoder_compress_buffer(struct encoder_t *encoder, struct device_t *dev, unsigned worker_number, unsigned buf_index);
