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

#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/mman.h>


#define MEMSINK_MAGIC	((uint64_t)0xCAFEBABECAFEBABE)
#define MEMSINK_VERSION	((uint32_t)2)

#ifndef CFG_MEMSINK_MAX_DATA
#	define CFG_MEMSINK_MAX_DATA 33554432
#endif
#define MEMSINK_MAX_DATA ((size_t)(CFG_MEMSINK_MAX_DATA))


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

	long double	grab_ts;
	long double	encode_begin_ts;
	long double	encode_end_ts;

	long double	last_client_ts;

	uint8_t		data[MEMSINK_MAX_DATA];
} memsink_shared_s;


INLINE memsink_shared_s *memsink_shared_map(int fd) {
	memsink_shared_s *mem = mmap(
		NULL,
		sizeof(memsink_shared_s),
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

INLINE int memsink_shared_unmap(memsink_shared_s *mem) {
	assert(mem != NULL);
	return munmap(mem, sizeof(memsink_shared_s));
}
