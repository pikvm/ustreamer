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

#include "types.h"
#include "frame.h"


typedef struct {
	uint	width;
	uint	height;
	bool	online;
} us_fpsi_meta_s;

typedef struct {
	char			*name;
	bool			with_meta;
	uint			accum;
	atomic_llong	state_sec_ts;
	atomic_ullong	state;
} us_fpsi_s;


us_fpsi_s *us_fpsi_init(const char *name, bool with_meta);
void us_fpsi_destroy(us_fpsi_s *fpsi);

void us_fpsi_frame_to_meta(const us_frame_s *frame, us_fpsi_meta_s *meta);
void us_fpsi_update(us_fpsi_s *fpsi, bool bump, const us_fpsi_meta_s *meta);
uint us_fpsi_get(us_fpsi_s *fpsi, us_fpsi_meta_s *meta);
