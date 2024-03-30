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

#include "types.h"
#include "frame.h"


typedef struct {
	US_FRAME_META_DECLARE;
} us_fpsi_meta_s;

typedef struct {
	char			*name;
	bool			with_meta;
	uint			accum;
	atomic_llong	ts;
	atomic_uint		current;
	us_fpsi_meta_s	meta;
	pthread_mutex_t	mutex;
} us_fpsi_s;


us_fpsi_s *us_fpsi_init(const char *name, bool with_meta);
void us_fpsi_destroy(us_fpsi_s *fpsi);

void us_fpsi_bump(us_fpsi_s *fpsi, const us_frame_s *frame, bool noop);
uint us_fpsi_get(us_fpsi_s *fpsi, us_fpsi_meta_s *meta);
