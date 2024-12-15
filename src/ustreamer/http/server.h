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

#include <sys/stat.h>

#include <event2/util.h>
#include <event2/event.h>
#include <event2/http.h>

#include "../../libs/types.h"
#include "../../libs/frame.h"
#include "../../libs/list.h"
#include "../../libs/fpsi.h"
#include "../encoder.h"
#include "../stream.h"


typedef struct {
	struct us_server_sx		*server;
	struct evhttp_request	*request;

	char	*key;
	bool	extra_headers;
	bool	advance_headers;
	bool	dual_final_frames;
	bool	zero_data;

	char	*hostport;
	u64		id;
	bool	need_initial;
	bool	need_first_frame;
	bool	updated_prev;

	us_fpsi_s *fpsi;

	US_LIST_DECLARE;
} us_stream_client_s;

typedef struct {
	struct us_server_sx		*server;
	struct evhttp_request	*request;
	ldf						request_ts;

	US_LIST_DECLARE;
} us_snapshot_client_s;

typedef struct {
	us_frame_s	*frame;
	us_fpsi_s	*queued_fpsi;
	uint		dropped;
	ldf			expose_begin_ts;
	ldf			expose_cmp_ts;
	ldf			expose_end_ts;
} us_server_exposed_s;

typedef struct {
	struct event_base	*base;
	struct evhttp		*http;
	evutil_socket_t		ext_fd; // Unix or socket activation

	char				*auth_token;

	struct event		*refresher;
	us_server_exposed_s	*exposed;

	us_stream_client_s	*stream_clients;
	uint				stream_clients_count;

	us_snapshot_client_s *snapshot_clients;
} us_server_runtime_s;

typedef struct us_server_sx {
	us_stream_s	*stream;

	char	*host;
	uint	port;

	char	*unix_path;
	bool	unix_rm;
	mode_t	unix_mode;

#	ifdef WITH_SYSTEMD
	bool	systemd;
#	endif

	bool	tcp_nodelay;
	uint	timeout;

	char	*user;
	char	*passwd;
	char	*static_path;
	char	*allow_origin;
	char	*instance_id;

	uint	drop_same_frames;
	uint	fake_width;
	uint	fake_height;

	us_server_runtime_s *run;
} us_server_s;


us_server_s *us_server_init(us_stream_s *stream);
void us_server_destroy(us_server_s *server);

int us_server_listen(us_server_s *server);
void us_server_loop(us_server_s *server);
void us_server_loop_break(us_server_s *server);
