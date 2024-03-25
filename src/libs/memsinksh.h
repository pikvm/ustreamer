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

#include "types.h"


#define US_MEMSINK_MAGIC	((u64)0xCAFEBABECAFEBABE)
#define US_MEMSINK_VERSION	((u32)6)


typedef struct {
	u64		magic;
	u32		version;

	u64		id;

	uz		used;
	uint	width;
	uint	height;
	uint	format;
	uint	stride;
	bool	online;
	bool	key;
	uint	gop;

	ldf		grab_ts;
	ldf		encode_begin_ts;
	ldf		encode_end_ts;

	ldf		last_client_ts;
	bool	key_requested;
} us_memsink_shared_s;


us_memsink_shared_s *us_memsink_shared_map(int fd, uz data_size);
int us_memsink_shared_unmap(us_memsink_shared_s *mem, uz data_size);

uz us_memsink_calculate_size(const char *obj);
u8 *us_memsink_get_data(us_memsink_shared_s *mem);
