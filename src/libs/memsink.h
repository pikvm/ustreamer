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

#include <sys/stat.h>

#include "types.h"
#include "frame.h"
#include "memsinksh.h"


typedef struct {
	const char	*name;
	const char	*obj;
	uz			data_size;
	bool		server;
	bool		rm;
	uint		client_ttl; // Only for server
	uint		timeout;

	int					fd;
	us_memsink_shared_s	*mem;

	u64			last_readed_id; // Only for client

	atomic_bool	has_clients; // Only for server results
	ldf			unsafe_last_client_ts; // Only for server
} us_memsink_s;


us_memsink_s *us_memsink_init_opened(
	const char *name, const char *obj, bool server,
	mode_t mode, bool rm, uint client_ttl, uint timeout);

void us_memsink_destroy(us_memsink_s *sink);

bool us_memsink_server_check(us_memsink_s *sink, const us_frame_s *frame);
int us_memsink_server_put(us_memsink_s *sink, const us_frame_s *frame, bool *key_requested);

int us_memsink_client_get(us_memsink_s *sink, us_frame_s *frame, bool *key_requested, bool key_required);
