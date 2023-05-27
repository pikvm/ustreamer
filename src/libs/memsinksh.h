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

#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/mman.h>


#define US_MEMSINK_MAGIC	((uint64_t)0xCAFEBABECAFEBABE)
#define US_MEMSINK_VERSION	((uint32_t)4)

#ifndef US_CFG_MEMSINK_MAX_DATA
#	define US_CFG_MEMSINK_MAX_DATA 33554432
#endif
#define US_MEMSINK_MAX_DATA ((size_t)(US_CFG_MEMSINK_MAX_DATA))


typedef struct {
	uint64_t	magic;
	uint32_t	version;

	uint64_t	id;

	size_t		used;
	unsigned	width;
	unsigned	height;
	unsigned	format;
	unsigned	stride;
	bool		online;
	bool		key;
	unsigned	gop;

	long double	grab_ts;
	long double	encode_begin_ts;
	long double	encode_end_ts;

	long double	last_client_ts;
	bool		key_requested;

	uint8_t		data[US_MEMSINK_MAX_DATA];
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
