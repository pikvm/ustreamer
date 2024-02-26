/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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

#include <sys/mman.h>

#include "types.h"


#define US_MEMSINK_MAGIC	((u64)0xCAFEBABECAFEBABE)
#define US_MEMSINK_VERSION	((u32)4)

#ifndef US_CFG_MEMSINK_MAX_DATA
#	define US_CFG_MEMSINK_MAX_DATA 33554432
#endif
#define US_MEMSINK_MAX_DATA ((uz)(US_CFG_MEMSINK_MAX_DATA))


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

	u8		data[US_MEMSINK_MAX_DATA];
} us_memsink_shared_s;


INLINE us_memsink_shared_s *us_memsink_shared_map(int fd) {
	us_memsink_shared_s *mem = mmap(
		NULL,
		sizeof(us_memsink_shared_s),
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		fd,
		0
	);
	if (mem == MAP_FAILED) {
		return NULL;
	}
	assert(mem != NULL);
	return mem;
}

INLINE int us_memsink_shared_unmap(us_memsink_shared_s *mem) {
	assert(mem != NULL);
	return munmap(mem, sizeof(us_memsink_shared_s));
}
