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


typedef struct stream_client_sx {
	struct server_sx *server;
	struct evhttp_request *request;

	char			*key;
	bool			extra_headers;
	bool			advance_headers;
	bool			dual_final_frames;
	bool			zero_data;

	char			*hostport;
	uint64_t		id;
	bool			need_initial;
	bool			need_first_frame;
	bool			updated_prev;
	unsigned		fps;
	unsigned		fps_accum;
	long long		fps_accum_second;

	LIST_STRUCT(struct stream_client_sx);
} stream_client_s;

typedef struct {
	frame_s			*frame;
	unsigned		captured_fps;
	unsigned		queued_fps;
	unsigned		dropped;
	long double		expose_begin_ts;
	long double		expose_cmp_ts;
	long double		expose_end_ts;

	bool		notify_last_online;
	unsigned	notify_last_width;
	unsigned	notify_last_height;
} exposed_s;

typedef struct {
	struct event_base	*base;
	struct evhttp		*http;
	evutil_socket_t		unix_fd;
	char				*auth_token;
	struct event		*refresh;
	stream_s			*stream;
	exposed_s			*exposed;
	stream_client_s		*stream_clients;
	unsigned			stream_clients_count;
} server_runtime_s;

typedef struct server_sx {
	char		*host;
	unsigned	port;
	char		*unix_path;
	bool		unix_rm;
	mode_t		unix_mode;
	bool		tcp_nodelay;
	unsigned	timeout;

	char		*user;
	char		*passwd;
	char		*static_path;
	char		*allow_origin;

	unsigned	drop_same_frames;
	unsigned	fake_width;
	unsigned	fake_height;

	bool		notify_parent;

	server_runtime_s *run;
} server_s;


server_s *server_init(stream_s *stream);
void server_destroy(server_s *server);

int server_listen(server_s *server);
void server_loop(server_s *server);
void server_loop_break(server_s *server);
