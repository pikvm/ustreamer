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
#include <strings.h>
#include <assert.h>

#include <pthread.h>
#include <linux/videodev2.h>

#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"
#include "../libs/frame.h"

#include "device.h"
#include "workers.h"

#include "encoders/cpu/encoder.h"
#include "encoders/hw/encoder.h"

#ifdef WITH_OMX
#	include "encoders/omx/encoder.h"
#	define ENCODER_TYPES_OMX_HINT ", OMX"
#else
#	define ENCODER_TYPES_OMX_HINT ""
#endif


#define ENCODER_TYPES_STR \
	"CPU, HW" \
	ENCODER_TYPES_OMX_HINT \
	", NOOP"

typedef enum {
	ENCODER_TYPE_UNKNOWN, // Only for encoder_parse_type() and main()
	ENCODER_TYPE_CPU,
	ENCODER_TYPE_HW,
#	ifdef WITH_OMX
	ENCODER_TYPE_OMX,
#	endif
	ENCODER_TYPE_NOOP,
} encoder_type_e;

typedef struct {
	encoder_type_e	type;
	unsigned		quality;
	bool			cpu_forced;
	pthread_mutex_t	mutex;

#	ifdef WITH_OMX
	unsigned		n_omxs;
	omx_encoder_s	**omxs;
#	endif
} encoder_runtime_s;

typedef struct {
	encoder_type_e	type;
	unsigned		n_workers;

	encoder_runtime_s *run;
} encoder_s;

typedef struct {
	encoder_s	*enc;
	hw_buffer_s	*hw;
	char		*dest_role;
	frame_s		*dest;
} encoder_job_s;


encoder_s *encoder_init(void);
void encoder_destroy(encoder_s *enc);

encoder_type_e encoder_parse_type(const char *str);
const char *encoder_type_to_string(encoder_type_e type);

workers_pool_s *encoder_workers_pool_init(encoder_s *enc, device_s *dev);
void encoder_get_runtime_params(encoder_s *enc, encoder_type_e *type, unsigned *quality);

int encoder_compress(encoder_s *enc, unsigned worker_number, frame_s *src, frame_s *dest);
