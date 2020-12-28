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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "../common/tools.h"
#include "../common/logging.h"
#include "../common/frame.h"


#ifndef CFG_RAWSINK_MAX_DATA
#	define CFG_RAWSINK_MAX_DATA 33554432
#endif
#define RAWSINK_MAX_DATA ((size_t)(CFG_RAWSINK_MAX_DATA))


typedef struct {
	size_t		used;
	unsigned	width;
	unsigned	height;
	unsigned	format;
	long double	grab_ts;
	uint8_t		data[RAWSINK_MAX_DATA];
} rawsink_shared_s;

typedef struct {
	bool		server;
	bool		rm;
	unsigned	timeout;

	char *mem_name;
	char *sig_name;

	int					fd;
	rawsink_shared_s	*mem;
	sem_t				*sig_sem;
} rawsink_s;


rawsink_s *rawsink_init(const char *name, bool server, mode_t mode, bool rm, unsigned timeout);
void rawsink_destroy(rawsink_s *rawsink);

int rawsink_server_put(rawsink_s *rawsink, frame_s *frame);
int rawsink_client_get(rawsink_s *rawsink, frame_s *frame);
