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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
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

#include <uuid/uuid.h>

#ifndef EVTHREAD_USE_PTHREADS_IMPLEMENTED
#	error Required libevent-pthreads support
#endif

#include "../../common/tools.h"
#include "../../common/threading.h"
#include "../../common/logging.h"
#include "../../common/process.h"
#include "../frame.h"
#include "../encoder.h"
#include "../stream.h"
#ifdef WITH_GPIO
#	include "../gpio/gpio.h"
#endif

#include "unix.h"
#include "uri.h"
#include "base64.h"
#include "mime.h"
#include "static.h"
#include "blank.h"

#include "data/index_html.h"


struct stream_client_t {
	struct http_server_t	*server;
	struct evhttp_request	*request;

	char					*key;
	bool					extra_headers;
	bool					advance_headers;
	bool					dual_final_frames;

	char					id[37]; // ex. "1b4e28ba-2fa1-11d2-883f-0016d3cca427" + "\0"
	bool					need_initial;
	bool					need_first_frame;
	bool					updated_prev;
	unsigned				fps;
	unsigned				fps_accum;
	long long				fps_accum_second;

	struct stream_client_t	*prev;
	struct stream_client_t	*next;
};

struct exposed_t {
	struct frame_t	*frame;
	unsigned			captured_fps;
	unsigned			queued_fps;
	bool				online;
	unsigned			dropped;
	long double			expose_begin_ts;
	long double			expose_cmp_ts;
	long double			expose_end_ts;
	long double			last_as_blank_ts;

	bool		notify_last_online;
	unsigned	notify_last_width;
	unsigned	notify_last_height;
};

struct http_server_runtime_t {
	struct event_base		*base;
	struct evhttp			*http;
	evutil_socket_t			unix_fd;
	char					*auth_token;
	struct event			*refresh;
	struct stream_t			*stream;
	struct exposed_t		*exposed;
	struct stream_client_t	*stream_clients;
	unsigned				stream_clients_count;
	struct frame_t			*blank;
	unsigned				drop_same_frames_blank;
};

struct http_server_t {
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

	char		*blank_path;
	int			last_as_blank;
	unsigned	drop_same_frames;
	bool		slowdown;
	unsigned	fake_width;
	unsigned	fake_height;

	bool		notify_parent;

	struct http_server_runtime_t *run;
};


struct http_server_t *http_server_init(struct stream_t *stream);
void http_server_destroy(struct http_server_t *server);

int http_server_listen(struct http_server_t *server);
void http_server_loop(struct http_server_t *server);
void http_server_loop_break(struct http_server_t *server);
