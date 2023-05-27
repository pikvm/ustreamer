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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/keyvalq_struct.h>

#ifndef EVTHREAD_USE_PTHREADS_IMPLEMENTED
#	error Required libevent-pthreads support
#endif

#include "../../libs/tools.h"
#include "../../libs/threading.h"
#include "../../libs/logging.h"
#include "../../libs/process.h"
#include "../../libs/frame.h"
#include "../../libs/base64.h"
#include "../../libs/list.h"
#include "../data/index_html.h"
#include "../data/favicon_ico.h"
#include "../encoder.h"
#include "../stream.h"
#ifdef WITH_GPIO
#	include "../gpio/gpio.h"
#endif

#include "bev.h"
#include "unix.h"
#include "uri.h"
#include "mime.h"
#include "static.h"
#ifdef WITH_SYSTEMD
#	include "systemd/systemd.h"
#endif


typedef struct us_stream_client_sx {
	struct us_server_sx		*server;
	struct evhttp_request	*request;

	char		*key;
	bool		extra_headers;
	bool		advance_headers;
	bool		dual_final_frames;
	bool		zero_data;

	char		*hostport;
	uint64_t	id;
	bool		need_initial;
	bool		need_first_frame;
	bool		updated_prev;
	unsigned	fps;
	unsigned	fps_accum;
	long long	fps_accum_second;

	US_LIST_STRUCT(struct us_stream_client_sx);
} us_stream_client_s;

typedef struct {
	us_frame_s		*frame;
	unsigned		captured_fps;
	unsigned		queued_fps;
	unsigned		dropped;
	long double		expose_begin_ts;
	long double		expose_cmp_ts;
	long double		expose_end_ts;

	bool			notify_last_online;
	unsigned		notify_last_width;
	unsigned		notify_last_height;
} us_exposed_s;

typedef struct {
	struct event_base	*base;
	struct evhttp		*http;
	evutil_socket_t		ext_fd; // Unix or socket activation

	char				*auth_token;

	struct event		*request_watcher;
	long double			last_request_ts;

	struct event		*refresher;
	us_stream_s			*stream;
	us_exposed_s			*exposed;

	us_stream_client_s	*stream_clients;
	unsigned			stream_clients_count;
} us_server_runtime_s;

typedef struct us_server_sx {
	char		*host;
	unsigned	port;

	char		*unix_path;
	bool		unix_rm;
	mode_t		unix_mode;

#	ifdef WITH_SYSTEMD
	bool		systemd;
#	endif

	bool		tcp_nodelay;
	unsigned	timeout;

	char		*user;
	char		*passwd;
	char		*static_path;
	char		*allow_origin;
	char		*instance_id;

	unsigned	drop_same_frames;
	unsigned	fake_width;
	unsigned	fake_height;

	bool		notify_parent;
	unsigned	exit_on_no_clients;

	us_server_runtime_s *run;
} us_server_s;


us_server_s *us_server_init(us_stream_s *stream);
void us_server_destroy(us_server_s *server);

int us_server_listen(us_server_s *server);
void us_server_loop(us_server_s *server);
void us_server_loop_break(us_server_s *server);
