/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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

#include <stdbool.h>
#include <semaphore.h>

#include <sys/types.h>
#include <sys/stat.h>


#ifndef CFG_RAWSINK_MAX_DATA
#	define CFG_RAWSINK_MAX_DATA 33554432
#endif
#define RAWSINK_MAX_DATA ((size_t)(CFG_RAWSINK_MAX_DATA))


struct rawsink_shared_t {
	unsigned        format;
	unsigned        width;
	unsigned        height;
	long double		grab_ts;
	size_t          size;
	unsigned char   data[RAWSINK_MAX_DATA];
};

struct rawsink_t {
	char	*mem_name;
	char	*signal_name;
	char	*lock_name;

	int		fd;
	struct rawsink_shared_t *shared;

	sem_t	*signal_sem;
	sem_t	*lock_sem;

	bool	rm;
	bool	master;

	bool	master_failed;
};


struct rawsink_t *rawsink_init(const char *name, mode_t mode, bool rm, bool master);
void rawsink_destroy(struct rawsink_t *rawsink);

void rawsink_put(
	struct rawsink_t *rawsink,
	const unsigned char *data, size_t size,
	unsigned format, unsigned witdh, unsigned height,
	long double grab_ts);

int rawsink_get(
	struct rawsink_t *rawsink,
	char *data, size_t *size,
	unsigned *format, unsigned *width, unsigned *height,
	long double *grab_ts,
	long double timeout);
