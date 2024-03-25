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


#include "memsinksh.h"

#include <string.h>
#include <strings.h>
#include <assert.h>

#include <sys/mman.h>

#include "types.h"


us_memsink_shared_s *us_memsink_shared_map(int fd, uz data_size) {
	us_memsink_shared_s *mem = mmap(
		NULL,
		sizeof(us_memsink_shared_s) + data_size,
		PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	if (mem == MAP_FAILED) {
		return NULL;
	}
	assert(mem != NULL);
	return mem;
}

int us_memsink_shared_unmap(us_memsink_shared_s *mem, uz data_size) {
	assert(mem != NULL);
	return munmap(mem, sizeof(us_memsink_shared_s) + data_size);
}

uz us_memsink_calculate_size(const char *obj) {
	const char *ptr = strrchr(obj, ':');
	if (ptr == NULL) {
		ptr = strrchr(obj, '.');
	}
	if (ptr != NULL) {
		ptr += 1;
		if (!strcasecmp(ptr, "jpeg")) {
			return 4 * 1024 * 1024;
		} else if (!strcasecmp(ptr, "h264")) {
			return 2 * 1024 * 1024;
		} else if (!strcasecmp(ptr, "raw")) {
			return 1920 * 1200 * 3; // RGB
		}
	}
	return 0;
}

u8 *us_memsink_get_data(us_memsink_shared_s *mem) {
	return (u8*)(mem) + sizeof(us_memsink_shared_s);
}
