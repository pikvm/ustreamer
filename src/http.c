/*****************************************************************************
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


#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <uuid/uuid.h>

#ifndef EVTHREAD_USE_PTHREADS_IMPLEMENTED
#	error Required libevent-pthreads support
#endif

#include "tools.h"
#include "logging.h"
#include "stream.h"
#include "http.h"

#include "data/blank.h"


static void _http_callback_root(struct evhttp_request *request, void *arg);
static void _http_callback_ping(struct evhttp_request *request, void *v_server);
static void _http_callback_snapshot(struct evhttp_request *request, void *v_server);

static void _http_callback_stream(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_ctx);
static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_ctx);

static void _http_exposed_refresh(int fd, short event, void *v_server);
static void _http_queue_send_stream(struct http_server_t *server, const bool updated);

static bool _expose_new_picture(struct http_server_t *server);
static bool _expose_blank_picture(struct http_server_t *server);


struct http_server_t *http_server_init(struct stream_t *stream) {
	struct http_server_runtime_t *run;
	struct http_server_t *server;
	struct exposed_t *exposed;
	struct timeval refresh_interval;

	A_CALLOC(exposed, 1);

	A_CALLOC(run, 1);
	run->stream = stream;
	run->exposed = exposed;
	run->drop_same_frames_blank = 10;

	A_CALLOC(server, 1);
	server->host = "127.0.0.1";
	server->port = 8080;
	server->timeout = 10;
	server->run = run;

	_expose_blank_picture(server);

	assert(!evthread_use_pthreads());
	assert((run->base = event_base_new()));
	assert((run->http = evhttp_new(run->base)));
	evhttp_set_allowed_methods(run->http, EVHTTP_REQ_GET|EVHTTP_REQ_HEAD);

	assert(!evhttp_set_cb(run->http, "/", _http_callback_root, NULL));
	assert(!evhttp_set_cb(run->http, "/ping", _http_callback_ping, (void *)server));
	assert(!evhttp_set_cb(run->http, "/snapshot", _http_callback_snapshot, (void *)server));
	assert(!evhttp_set_cb(run->http, "/stream", _http_callback_stream, (void *)server));

	refresh_interval.tv_sec = 0;
	refresh_interval.tv_usec = 30000; // ~30 refreshes per second
	assert((run->refresh = event_new(run->base, -1, EV_PERSIST, _http_exposed_refresh, server)));
	assert(!event_add(run->refresh, &refresh_interval));

	return server;
}

void http_server_destroy(struct http_server_t *server) {
	event_del(server->run->refresh);
	event_free(server->run->refresh);
	evhttp_free(server->run->http);
	event_base_free(server->run->base);
	libevent_global_shutdown();

	free(server->run->exposed->picture.data);
	free(server->run->exposed);
	free(server->run);
	free(server);
}

int http_server_listen(struct http_server_t *server) {
	server->run->drop_same_frames_blank = max_u(server->drop_same_frames, server->run->drop_same_frames_blank);

	LOG_DEBUG("Binding HTTP to [%s]:%d ...", server->host, server->port);
	evhttp_set_timeout(server->run->http, server->timeout);

	if (evhttp_bind_socket(server->run->http, server->host, server->port) < 0) {
		LOG_PERROR("Can't listen HTTP on [%s]:%d", server->host, server->port)
		return -1;
	}

	LOG_INFO("Listening HTTP on [%s]:%d", server->host, server->port);
	return 0;
}

void http_server_loop(struct http_server_t *server) {
	LOG_INFO("Starting HTTP eventloop ...");
	event_base_dispatch(server->run->base);
	LOG_INFO("HTTP eventloop stopped");
}

void http_server_loop_break(struct http_server_t *server) {
	event_base_loopbreak(server->run->base);
}


#define ADD_HEADER(_key, _value) \
	assert(!evhttp_add_header(evhttp_request_get_output_headers(request), _key, _value))

#define PROCESS_HEAD_REQUEST { \
		if (evhttp_request_get_command(request) == EVHTTP_REQ_HEAD) { \
			evhttp_send_reply(request, HTTP_OK, "OK", NULL); \
			return; \
		} \
	}

static void _http_callback_root(struct evhttp_request *request, UNUSED void *arg) {
	struct evbuffer *buf;

	PROCESS_HEAD_REQUEST;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf,
		"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
		"<title>uStreamer</title></head><body><ul>"
		"<li><a href=\"/ping\">/ping</a></li>"
		"<li><a href=\"/snapshot\">/snapshot</a></li>"
		"<li><a href=\"/stream\">/stream</a></li>"
		"</body></html>"
	));
	ADD_HEADER("Content-Type", "text/html");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_ping(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;

	PROCESS_HEAD_REQUEST;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf,
		"{\"source\": {\"resolution\": {\"width\": %u, \"height\": %u},"
		" \"online\": %s, \"quality\": %u, \"captured_fps\": %u},"
		" \"stream\": {\"queued_fps\": %u,  \"clients\": %u, \"clients_stat\": {",
		(server->fake_width ? server->fake_width : server->run->exposed->width),
		(server->fake_height ? server->fake_height : server->run->exposed->height),
		(server->run->exposed->online ? "true" : "false"),
		server->run->stream->encoder->quality,
		server->run->exposed->captured_fps,
		server->run->exposed->queued_fps,
		server->run->stream_clients_count
	));
	for (struct stream_client_t * client = server->run->stream_clients; client != NULL; client = client->next) {
		assert(evbuffer_add_printf(buf,
			"\"%s\": {\"fps\": %u}%s",
			client->id, client->fps, (client->next ? ", " : "")
		));
	}
	assert(evbuffer_add_printf(buf, "}}}"));

	ADD_HEADER("Content-Type", "application/json");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_snapshot(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;
	char time_buf[64];

	PROCESS_HEAD_REQUEST;

#	define EXPOSED(_next) server->run->exposed->_next

	assert((buf = evbuffer_new()));
	assert(!evbuffer_add(buf, (const void *)EXPOSED(picture.data), EXPOSED(picture.size)));

	ADD_HEADER("Access-Control-Allow-Origin:", "*");
	ADD_HEADER("Cache-Control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
	ADD_HEADER("Pragma", "no-cache");
	ADD_HEADER("Expires", "Mon, 3 Jan 2000 12:34:56 GMT");

#	define ADD_TIME_HEADER(_key, _value) \
		{ sprintf(time_buf, "%.06Lf", _value); ADD_HEADER(_key, time_buf); }

	ADD_TIME_HEADER("X-Timestamp", get_now_real());

	ADD_HEADER("X-UStreamer-Online",					(EXPOSED(online) ? "true" : "false"));
	ADD_TIME_HEADER("X-UStreamer-Grab-Time",			EXPOSED(picture.grab_time));
	ADD_TIME_HEADER("X-UStreamer-Encode-Begin-Time",	EXPOSED(picture.encode_begin_time));
	ADD_TIME_HEADER("X-UStreamer-Encode-End-Time",		EXPOSED(picture.encode_end_time));
	ADD_TIME_HEADER("X-UStreamer-Expose-Begin-Time",	EXPOSED(expose_begin_time));
	ADD_TIME_HEADER("X-UStreamer-Expose-Cmp-Time",		EXPOSED(expose_cmp_time));
	ADD_TIME_HEADER("X-UStreamer-Expose-End-Time",		EXPOSED(expose_end_time));
	ADD_TIME_HEADER("X-UStreamer-Send-Time",			get_now_monotonic());

#	undef ADD_TIME_HEADER

	ADD_HEADER("Content-Type", "image/jpeg");

	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);

#	undef EXPOSED
}

#undef ADD_HEADER

static void _http_callback_stream(struct evhttp_request *request, void *v_server) {
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L2814
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L2789
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L362
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L791
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L1458

	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evhttp_connection *conn;
	struct bufferevent *buf_event;
	struct stream_client_t *client;
	char *client_addr;
	unsigned short client_port;
	uuid_t uuid;

	PROCESS_HEAD_REQUEST;

	conn = evhttp_request_get_connection(request);
	if (conn != NULL) {
		A_CALLOC(client, 1);
		client->server = server;
		client->request = request;
		client->need_initial = true;
		client->need_first_frame = true;

		uuid_generate(uuid);
		uuid_unparse_lower(uuid, client->id);

		if (server->run->stream_clients == NULL) {
			server->run->stream_clients = client;
		} else {
			struct stream_client_t *last = server->run->stream_clients;

			for (; last->next != NULL; last = last->next);
			client->prev = last;
			last->next = client;
		}
		server->run->stream_clients_count += 1;

		evhttp_connection_get_peer(conn, &client_addr, &client_port);
		LOG_INFO(
			"HTTP: Registered the new stream client: [%s]:%u; id=%s; clients now: %u",
			client_addr, client_port, client->id, server->run->stream_clients_count
		);

		buf_event = evhttp_connection_get_bufferevent(conn);
		bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
		bufferevent_enable(buf_event, EV_READ);
	} else {
		evhttp_request_free(request);
	}
}

#undef PROCESS_HEAD_REQUEST

static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_client) {
#	define BOUNDARY "boundarydonotcross"
#	define RN "\r\n"

	struct stream_client_t *client = (struct stream_client_t *)v_client;
	struct evbuffer *buf;
	long double now = get_now_monotonic();
	long long now_second = floor_ms(now);

	if (now_second != client->fps_accum_second) {
		client->fps = client->fps_accum;
		client->fps_accum = 0;
		client->fps_accum_second = now_second;
	}
	client->fps_accum += 1;

	assert((buf = evbuffer_new()));

	if (client->need_initial) {
		assert(evbuffer_add_printf(buf,
			"HTTP/1.0 200 OK" RN
			"Access-Control-Allow-Origin: *" RN
			"Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0" RN
			"Pragma: no-cache" RN
			"Expires: Mon, 3 Jan 2000 12:34:56 GMT" RN
			"Set-Cookie: stream_client_id=%s; path=/; max-age=30" RN
			"Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY RN
			RN
			"--" BOUNDARY RN,
			client->id
		));
		assert(!bufferevent_write_buffer(buf_event, buf));
		client->need_initial = false;
	}

#	define EXPOSED(_next) client->server->run->exposed->_next

	assert(evbuffer_add_printf(buf,
		"Content-Type: image/jpeg" RN
		"Content-Length: %lu" RN
		"X-Timestamp: %.06Lf" RN
		"%s",
		EXPOSED(picture.size) * sizeof(*EXPOSED(picture.data)),
		get_now_real(), (client->server->extra_stream_headers ? "" : RN)
	));
	if (client->server->extra_stream_headers) {
		assert(evbuffer_add_printf(buf,
			"X-UStreamer-Online: %s" RN
			"X-UStreamer-Client-FPS: %u" RN
			"X-UStreamer-Grab-Time: %.06Lf" RN
			"X-UStreamer-Encode-Begin-Time: %.06Lf" RN
			"X-UStreamer-Encode-End-Time: %.06Lf" RN
			"X-UStreamer-Expose-Begin-Time: %.06Lf" RN
			"X-UStreamer-Expose-Cmp-Time: %.06Lf" RN
			"X-UStreamer-Expose-End-Time: %.06Lf" RN
			"X-UStreamer-Send-Time: %.06Lf" RN
			RN,
			(EXPOSED(online) ? "true" : "false"),
			client->fps,
			EXPOSED(picture.grab_time),
			EXPOSED(picture.encode_begin_time),
			EXPOSED(picture.encode_end_time),
			EXPOSED(expose_begin_time),
			EXPOSED(expose_cmp_time),
			EXPOSED(expose_end_time),
			now
		));
	}

	assert(!evbuffer_add(buf,
		(void *)EXPOSED(picture.data),
		EXPOSED(picture.size) * sizeof(*EXPOSED(picture.data))
	));
	assert(evbuffer_add_printf(buf, RN "--" BOUNDARY RN));

	assert(!bufferevent_write_buffer(buf_event, buf));
	evbuffer_free(buf);

	bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
	bufferevent_enable(buf_event, EV_READ);

#	undef BOUNDARY
#	undef RN
#	undef EXPOSED
}

static void _http_callback_stream_error(UNUSED struct bufferevent *buf_event, UNUSED short what, void *v_client) {
	struct stream_client_t *client = (struct stream_client_t *)v_client;
	struct evhttp_connection *conn;
	char *client_addr = "???";
	unsigned short client_port = 0;

	client->server->run->stream_clients_count -= 1;

	conn = evhttp_request_get_connection(client->request);
	if (conn != NULL) {
		evhttp_connection_get_peer(conn, &client_addr, &client_port);
	}
	LOG_INFO(
		"HTTP: Disconnected the stream client: [%s]:%u; clients now: %u",
		client_addr, client_port, client->server->run->stream_clients_count
	);
	if (conn != NULL) {
		evhttp_connection_free(conn);
	}

	if (client->prev == NULL) {
		client->server->run->stream_clients = client->next;
	} else {
		client->prev->next = client->next;
	}
	if (client->next != NULL) {
		client->next->prev = client->prev;
	}
	free(client);
}

static void _http_queue_send_stream(struct http_server_t *server, const bool updated) {
	struct evhttp_connection *conn;
	struct bufferevent *buf_event;
	long long now;
	bool queued = false;
	static unsigned queued_fps_accum = 0;
	static long long queued_fps_second = 0;

	for (struct stream_client_t *client = server->run->stream_clients; client != NULL; client = client->next) {
		conn = evhttp_request_get_connection(client->request);
		if (conn != NULL && (updated || client->need_first_frame)) {
			buf_event = evhttp_connection_get_bufferevent(conn);
			bufferevent_setcb(buf_event, NULL, _http_callback_stream_write, _http_callback_stream_error, (void *)client);
			bufferevent_enable(buf_event, EV_READ|EV_WRITE);
			client->need_first_frame = false;
			queued = true;
		}
	}

	if (queued) {
		if ((now = floor_ms(get_now_monotonic())) != queued_fps_second) {
			server->run->exposed->queued_fps = queued_fps_accum;
			queued_fps_accum = 0;
			queued_fps_second = now;
		}
		queued_fps_accum += 1;
	}
}

static void _http_exposed_refresh(UNUSED int fd, UNUSED short what, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	bool updated = false;
	bool queue_send = false;

#define LOCK_STREAM \
	A_PTHREAD_M_LOCK(&server->run->stream->mutex);

#define UNLOCK_STREAM \
	{ server->run->stream->updated = false; A_PTHREAD_M_UNLOCK(&server->run->stream->mutex); }

	if (server->run->stream->updated) {
		LOG_DEBUG("Refreshing HTTP exposed ...");
		LOCK_STREAM;
		if (server->run->stream->picture.size > 0) { // If online
			updated = _expose_new_picture(server);
			UNLOCK_STREAM;
		} else {
			UNLOCK_STREAM;
			updated = _expose_blank_picture(server);
		}
		queue_send = true;
	} else if (!server->run->exposed->online) {
		LOG_DEBUG("Refreshing HTTP exposed (BLANK) ...");
		updated = _expose_blank_picture(server);
		queue_send = true;
	}

	if (queue_send) {
		if (server->drop_same_frames) {
			// Хром всегда показывает не новый пришедший фрейм, а предыдущий.
			// При updated == false нужно еще один раз послать предыдущий фрейм
			// https://bugs.chromium.org/p/chromium/issues/detail?id=527446

			static bool updated_prev = false;
			bool updated_orig = updated;

			if (updated_prev && !updated_orig) {
				updated = true;
			}
			updated_prev = updated_orig;
		}

		_http_queue_send_stream(server, updated);
	}

#	undef LOCK_STREAM
#	undef UNLOCK_STREAM
}

static bool _expose_new_picture(struct http_server_t *server) {
#	define STREAM(_next) server->run->stream->_next
#	define EXPOSED(_next) server->run->exposed->_next

	assert(STREAM(picture.size) > 0);
	EXPOSED(captured_fps) = STREAM(captured_fps);
	EXPOSED(expose_begin_time) = get_now_monotonic();

#	define MEM_STREAM_TO_EXPOSED \
		EXPOSED(picture.data), STREAM(picture.data), \
		STREAM(picture.size) * sizeof(*STREAM(picture.data))

	if (server->drop_same_frames) {
		if (
			EXPOSED(online)
			&& EXPOSED(dropped) < server->drop_same_frames
			&& EXPOSED(picture.size) == STREAM(picture.size)
			&& !memcmp(MEM_STREAM_TO_EXPOSED)
		) {
			EXPOSED(expose_cmp_time) = get_now_monotonic();
			EXPOSED(expose_end_time) = EXPOSED(expose_cmp_time);
			LOG_PERF(
				"HTTP: dropped same frame number %u; comparsion time = %.06Lf",
				EXPOSED(dropped), EXPOSED(expose_cmp_time) - EXPOSED(expose_begin_time)
			);
			EXPOSED(dropped) += 1;
			return false; // Not updated
		} else {
			EXPOSED(expose_cmp_time) = get_now_monotonic();
			LOG_PERF(
				"HTTP: passed same frame check (frames are differ); comparsion time = %.06Lf",
				EXPOSED(expose_cmp_time) - EXPOSED(expose_begin_time)
			);
		}
	}

	if (EXPOSED(picture.allocated) < STREAM(picture.allocated)) {
		A_REALLOC(EXPOSED(picture.data), STREAM(picture.allocated));
		EXPOSED(picture.allocated) = STREAM(picture.allocated);
	}

	memcpy(MEM_STREAM_TO_EXPOSED);

#	undef MEM_STREAM_TO_EXPOSED

	EXPOSED(picture.size) = STREAM(picture.size);

	EXPOSED(picture.grab_time) = STREAM(picture.grab_time);
	EXPOSED(picture.encode_begin_time) = STREAM(picture.encode_begin_time);
	EXPOSED(picture.encode_end_time) = STREAM(picture.encode_end_time);

	EXPOSED(width) = STREAM(width);
	EXPOSED(height) = STREAM(height);
	EXPOSED(online) = true;
	EXPOSED(dropped) = 0;
	EXPOSED(expose_cmp_time) = EXPOSED(expose_begin_time);
	EXPOSED(expose_end_time) = get_now_monotonic();

#	undef STREAM
#	undef EXPOSED
	return true; // Updated
}

static bool _expose_blank_picture(struct http_server_t *server) {
#	define EXPOSED(_next) server->run->exposed->_next

	EXPOSED(expose_begin_time) = get_now_monotonic();
	EXPOSED(expose_cmp_time) = EXPOSED(expose_begin_time);

	if (EXPOSED(online) || EXPOSED(picture.size) == 0) {
		if (EXPOSED(picture.allocated) < BLANK_JPG_SIZE) {
			A_REALLOC(EXPOSED(picture.data), BLANK_JPG_SIZE);
			EXPOSED(picture.allocated) = BLANK_JPG_SIZE;
		}

		memcpy(
			EXPOSED(picture.data), BLANK_JPG_DATA,
			BLANK_JPG_SIZE * sizeof(*EXPOSED(picture.data))
		);

		EXPOSED(picture.size) = BLANK_JPG_SIZE;

		EXPOSED(picture.grab_time) = 0;
		EXPOSED(picture.encode_begin_time) = 0;
		EXPOSED(picture.encode_end_time) = 0;

		EXPOSED(width) = BLANK_JPG_WIDTH;
		EXPOSED(height) = BLANK_JPG_HEIGHT;
		EXPOSED(captured_fps) = 0;
		EXPOSED(online) = false;
		goto updated;
	}

	if (EXPOSED(dropped) < server->run->drop_same_frames_blank) {
		LOG_PERF("HTTP: dropped same frame (BLANK) number %u", EXPOSED(dropped));
		EXPOSED(dropped) += 1;
		EXPOSED(expose_end_time) = get_now_monotonic();
		return false; // Not updated
	}

	updated:
		EXPOSED(dropped) = 0;
		EXPOSED(expose_end_time) = get_now_monotonic();
		return true; // Updated

#	undef EXPOSED
}
