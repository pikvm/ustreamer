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
#include <stdatomic.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "tools.h"
#include "logging.h"
#include "frame.h"
#include "memsinksh.h"


typedef struct {
	const char			*name;
	const char			*obj;
	bool				server;
	bool				rm;
	unsigned			client_ttl; // Only for server
	unsigned			timeout;

	int					fd;
	us_memsink_shared_s	*mem;
	uint64_t			last_id;
	atomic_bool			has_clients; // Only for server
} us_memsink_s;


us_memsink_s *us_memsink_init(
	const char *name, const char *obj, bool server,
	mode_t mode, bool rm, unsigned client_ttl, unsigned timeout);

void us_memsink_destroy(us_memsink_s *sink);

bool us_memsink_server_check(us_memsink_s *sink, const us_frame_s *frame);
int us_memsink_server_put(us_memsink_s *sink, const us_frame_s *frame, bool *const key_requested);

int us_memsink_client_get(us_memsink_s *sink, us_frame_s *frame, bool *const key_requested, bool key_required);
