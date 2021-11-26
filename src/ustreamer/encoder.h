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
#include <math.h>
#include <assert.h>

#include <pthread.h>
#include <linux/videodev2.h>

#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"
#include "../libs/frame.h"

#include "device.h"
#include "workers.h"
#include "m2m.h"

#include "encoders/cpu/encoder.h"
#include "encoders/hw/encoder.h"


#ifndef CFG_ENCODER_M2M_BITRATE_MIN
#	define CFG_ENCODER_M2M_BITRATE_MIN 25
#endif
#define ENCODER_M2M_BITRATE_MIN ((unsigned)CFG_ENCODER_M2M_BITRATE_MIN)

#ifndef CFG_ENCODER_M2M_BITRATE_MAX
#	define CFG_ENCODER_M2M_BITRATE_MAX 25000
#endif
#define ENCODER_M2M_BITRATE_MAX ((unsigned)CFG_ENCODER_M2M_BITRATE_MAX)

#ifndef CFG_ENCODER_M2M_BITRATE_STEP
#	define CFG_ENCODER_M2M_BITRATE_STEP 25
#endif
#define ENCODER_M2M_BITRATE_STEP ((unsigned)CFG_ENCODER_M2M_BITRATE_STEP)


#define ENCODER_TYPES_STR "CPU, HW, M2M, NOOP"

typedef enum {
	ENCODER_TYPE_UNKNOWN, // Only for encoder_parse_type() and main()
	ENCODER_TYPE_CPU,
	ENCODER_TYPE_HW,
	ENCODER_TYPE_M2M,
	ENCODER_TYPE_NOOP,
} encoder_type_e;

typedef struct {
	encoder_type_e	type;
	unsigned		quality;
	bool			cpu_forced;
	pthread_mutex_t	mutex;

	unsigned		n_m2ms;
	m2m_encoder_s	**m2ms;
} encoder_runtime_s;

typedef struct {
	encoder_type_e	type;
	unsigned		n_workers;
	char			*m2m_path;

	encoder_runtime_s *run;
} encoder_s;

typedef struct {
	encoder_s	*enc;
	hw_buffer_s	*hw;
	frame_s		*dest;
} encoder_job_s;


encoder_s *encoder_init(void);
void encoder_destroy(encoder_s *enc);

encoder_type_e encoder_parse_type(const char *str);
const char *encoder_type_to_string(encoder_type_e type);

workers_pool_s *encoder_workers_pool_init(encoder_s *enc, device_s *dev);
void encoder_get_runtime_params(encoder_s *enc, encoder_type_e *type, unsigned *quality);

int encoder_compress(encoder_s *enc, unsigned worker_number, frame_s *src, frame_s *dest);
