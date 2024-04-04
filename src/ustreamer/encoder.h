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

#include <pthread.h>

#include "../libs/types.h"
#include "../libs/frame.h"
#include "../libs/capture.h"

#include "workers.h"
#include "m2m.h"


#define ENCODER_TYPES_STR "CPU, HW, M2M-VIDEO, M2M-IMAGE"


typedef enum {
	US_ENCODER_TYPE_CPU,
	US_ENCODER_TYPE_HW,
	US_ENCODER_TYPE_M2M_VIDEO,
	US_ENCODER_TYPE_M2M_IMAGE,
} us_encoder_type_e;

typedef struct {
	us_encoder_type_e	type;
	uint				quality;
	pthread_mutex_t		mutex;

	uint				n_m2ms;
	us_m2m_encoder_s	**m2ms;

	us_workers_pool_s	*pool;
} us_encoder_runtime_s;

typedef struct {
	us_encoder_type_e	type;
	uint				n_workers;
	char				*m2m_path;

	us_encoder_runtime_s *run;
} us_encoder_s;

typedef struct {
	us_encoder_s		*enc;
	us_capture_hwbuf_s	*hw;
	us_frame_s			*dest;
} us_encoder_job_s;


us_encoder_s *us_encoder_init(void);
void us_encoder_destroy(us_encoder_s *enc);

int us_encoder_parse_type(const char *str);
const char *us_encoder_type_to_string(us_encoder_type_e type);

void us_encoder_open(us_encoder_s *enc, us_capture_s *cap);
void us_encoder_close(us_encoder_s *enc);

void us_encoder_get_runtime_params(us_encoder_s *enc, us_encoder_type_e *type, uint *quality);
