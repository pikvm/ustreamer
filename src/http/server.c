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


#include "server.h"

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

#include "../tools.h"
#include "../threading.h"
#include "../logging.h"
#include "../process.h"
#include "../picture.h"
#include "../encoder.h"
#include "../stream.h"
#ifdef WITH_GPIO
#	include "../gpio.h"
#endif

#include "unix.h"
#include "uri.h"
#include "base64.h"
#include "mime.h"
#include "static.h"
#include "blank.h"

#include "data/index_html.h"


static int _http_preprocess_request(struct evhttp_request *request, struct http_server_t *server);

static void _http_callback_root(struct evhttp_request *request, void *v_server);
static void _http_callback_static(struct evhttp_request *request, void *v_server);
static void _http_callback_state(struct evhttp_request *request, void *v_server);
static void _http_callback_snapshot(struct evhttp_request *request, void *v_server);

static void _http_callback_stream(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_ctx);
static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_ctx);

static void _http_exposed_refresh(int fd, short event, void *v_server);
static void _http_queue_send_stream(struct http_server_t *server, bool stream_updated, bool picture_updated);

static bool _expose_new_picture_unsafe(struct http_server_t *server);
static bool _expose_blank_picture(struct http_server_t *server);

static void _format_bufferevent_reason(short what, char *reason);


struct http_server_t *http_server_init(struct stream_t *stream) {
	struct http_server_runtime_t *run;
	struct http_server_t *server;
	struct exposed_t *exposed;

	A_CALLOC(exposed, 1);
	exposed->picture = picture_init();

	A_CALLOC(run, 1);
	run->stream = stream;
	run->exposed = exposed;
	run->drop_same_frames_blank = 10;

	A_CALLOC(server, 1);
	server->host = "127.0.0.1";
	server->port = 8080;
	server->unix_path = "";
	server->user = "";
	server->passwd = "";
	server->static_path = "";
	server->timeout = 10;
	server->last_as_blank = -1;
	server->run = run;

	assert(!evthread_use_pthreads());
	assert((run->base = event_base_new()));
	assert((run->http = evhttp_new(run->base)));
	evhttp_set_allowed_methods(run->http, EVHTTP_REQ_GET|EVHTTP_REQ_HEAD);
	return server;
}

void http_server_destroy(struct http_server_t *server) {
	if (server->run->refresh) {
		event_del(server->run->refresh);
		event_free(server->run->refresh);
	}

	evhttp_free(server->run->http);
	if (server->run->unix_fd) {
		close(server->run->unix_fd);
	}
	event_base_free(server->run->base);

#	if LIBEVENT_VERSION_NUMBER >= 0x02010100
	libevent_global_shutdown();
#	endif

	for (struct stream_client_t *client = server->run->stream_clients; client != NULL;) {
		struct stream_client_t *next = client->next;

		free(client->key);
		free(client);
		client = next;
	}

	if (server->run->auth_token) {
		free(server->run->auth_token);
	}

	if (server->run->blank) {
		picture_destroy(server->run->blank);
	}

	picture_destroy(server->run->exposed->picture);
	free(server->run->exposed);
	free(server->run);
	free(server);
}

int http_server_listen(struct http_server_t *server) {
	{
		if (server->static_path[0] != '\0') {
			LOG_INFO("Enabling HTTP file server: %s", server->static_path);
			evhttp_set_gencb(server->run->http, _http_callback_static, (void *)server);
		} else {
			assert(!evhttp_set_cb(server->run->http, "/", _http_callback_root, (void *)server));
		}
		assert(!evhttp_set_cb(server->run->http, "/state", _http_callback_state, (void *)server));
		assert(!evhttp_set_cb(server->run->http, "/snapshot", _http_callback_snapshot, (void *)server));
		assert(!evhttp_set_cb(server->run->http, "/stream", _http_callback_stream, (void *)server));
	}

	server->run->drop_same_frames_blank = max_u(server->drop_same_frames, server->run->drop_same_frames_blank);
	server->run->blank = blank_picture_init(server->blank_path);

#	define EXPOSED(_next) server->run->exposed->_next
	// See _expose_blank_picture()
	picture_copy(server->run->blank, EXPOSED(picture));
	EXPOSED(expose_begin_ts) = get_now_monotonic();
	EXPOSED(expose_cmp_ts) = EXPOSED(expose_begin_ts);
	EXPOSED(expose_end_ts) = EXPOSED(expose_begin_ts);
	// See _http_exposed_refresh()
	EXPOSED(notify_last_width) = EXPOSED(picture->width);
	EXPOSED(notify_last_height) = EXPOSED(picture->height);
#	undef EXPOSED

	{
		struct timeval refresh_interval;

		refresh_interval.tv_sec = 0;
		if (server->run->stream->dev->desired_fps > 0) {
			refresh_interval.tv_usec = 1000000 / (server->run->stream->dev->desired_fps * 2);
		} else {
			refresh_interval.tv_usec = 16000; // ~60fps
		}

		assert((server->run->refresh = event_new(server->run->base, -1, EV_PERSIST, _http_exposed_refresh, server)));
		assert(!event_add(server->run->refresh, &refresh_interval));
	}

	if (server->slowdown) {
		stream_switch_slowdown(server->run->stream, true);
	}

	evhttp_set_timeout(server->run->http, server->timeout);

	if (server->user[0] != '\0') {
		char *raw_token;
		char *encoded_token;

		A_CALLOC(raw_token, strlen(server->user) + strlen(server->passwd) + 2);
		sprintf(raw_token, "%s:%s", server->user, server->passwd);
		encoded_token = base64_encode((unsigned char *)raw_token);
		free(raw_token);

		A_CALLOC(server->run->auth_token, strlen(encoded_token) + 16);
		sprintf(server->run->auth_token, "Basic %s", encoded_token);
		free(encoded_token);

		LOG_INFO("Using HTTP basic auth");
	}

	if (server->unix_path[0] != '\0') {
		LOG_DEBUG("Binding HTTP to UNIX socket '%s' ...", server->unix_path);
		if ((server->run->unix_fd = evhttp_my_bind_unix(
			server->run->http,
			server->unix_path,
			server->unix_rm,
			server->unix_mode)) < 0
		) {
			return -1;
		}
		LOG_INFO("Listening HTTP on UNIX socket '%s'", server->unix_path);
		if (server->tcp_nodelay) {
			LOG_ERROR("TCP_NODELAY flag can't be used with UNIX socket and will be ignored");
		}
	} else {
		LOG_DEBUG("Binding HTTP to [%s]:%u ...", server->host, server->port);
		if (evhttp_bind_socket(server->run->http, server->host, server->port) < 0) {
			LOG_PERROR("Can't bind HTTP on [%s]:%u", server->host, server->port)
			return -1;
		}
		LOG_INFO("Listening HTTP on [%s]:%u", server->host, server->port);
	}

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

static int _http_preprocess_request(struct evhttp_request *request, struct http_server_t *server) {
	if (server->run->auth_token) {
		const char *token = evhttp_find_header(evhttp_request_get_input_headers(request), "Authorization");

		if (token == NULL || strcmp(token, server->run->auth_token) != 0) {
			ADD_HEADER("WWW-Authenticate", "Basic realm=\"Restricted area\"");
			evhttp_send_reply(request, 401, "Unauthorized", NULL);
			return -1;
		}
	}

	if (evhttp_request_get_command(request) == EVHTTP_REQ_HEAD) { \
		evhttp_send_reply(request, HTTP_OK, "OK", NULL); \
		return -1;
	}

	return 0;
}

#define PREPROCESS_REQUEST { \
		if (_http_preprocess_request(request, server) < 0) { \
			return; \
		} \
	}

static void _http_callback_root(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;

	PREPROCESS_REQUEST;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf, "%s", HTML_INDEX_PAGE));
	ADD_HEADER("Content-Type", "text/html");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_static(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf = NULL;
	struct evhttp_uri *uri = NULL;
	char *uri_path;
	char *decoded_path = NULL;
	char *static_path = NULL;
	int fd = -1;
	struct stat st;

	PREPROCESS_REQUEST;

	if ((uri = evhttp_uri_parse(evhttp_request_get_uri(request))) == NULL) {
		goto bad_request;
	}
	if ((uri_path = (char *)evhttp_uri_get_path(uri)) == NULL) {
		uri_path = "/";
	}

	if ((decoded_path = evhttp_uridecode(uri_path, 0, NULL)) == NULL) {
		goto bad_request;
	}

	assert((buf = evbuffer_new()));

	if ((static_path = find_static_file_path(server->static_path, decoded_path)) == NULL) {
		goto not_found;
	}

	if ((fd = open(static_path, O_RDONLY)) < 0) {
		LOG_PERROR("HTTP: Can't open found static file %s", static_path);
		goto not_found;
	}

	if (fstat(fd, &st) < 0) {
		LOG_PERROR("HTTP: Can't stat() found static file %s", static_path);
		goto not_found;
	}

	if (st.st_size > 0 && evbuffer_add_file(buf, fd, 0, st.st_size) < 0) {
		LOG_ERROR("HTTP: Can't serve static file %s", static_path);
		goto not_found;
	}
	ADD_HEADER("Content-Type", guess_mime_type(static_path));
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	goto cleanup;

	bad_request:
		evhttp_send_error(request, HTTP_BADREQUEST, NULL);
		goto cleanup;

	not_found:
		evhttp_send_error(request, HTTP_NOTFOUND, NULL);
		goto cleanup;

	cleanup:
		if (fd >= 0) {
			close(fd);
		}
		if (static_path) {
			free(static_path);
		}
		if (buf) {
			evbuffer_free(buf);
		}
		if (decoded_path) {
			free(decoded_path);
		}
		if (uri) {
			evhttp_uri_free(uri);
		}
}

static void _http_callback_state(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;
	enum encoder_type_t encoder_run_type;
	unsigned encoder_run_quality;

	PREPROCESS_REQUEST;

#	define ENCODER(_next) server->run->stream->encoder->_next
#	define EXPOSED(_next) server->run->exposed->_next

	A_MUTEX_LOCK(&ENCODER(run->mutex));
	encoder_run_type = ENCODER(run->type);
	encoder_run_quality = ENCODER(run->quality);
	A_MUTEX_UNLOCK(&ENCODER(run->mutex));

	assert((buf = evbuffer_new()));

	assert(evbuffer_add_printf(buf,
		"{\"ok\": true, \"result\": {"
		" \"encoder\": {\"type\": \"%s\", \"quality\": %u},"
		" \"source\": {\"resolution\": {\"width\": %u, \"height\": %u},"
		" \"online\": %s, \"desired_fps\": %u, \"captured_fps\": %u},"
		" \"stream\": {\"queued_fps\": %u, \"clients\": %u, \"clients_stat\": {",
		encoder_type_to_string(encoder_run_type),
		encoder_run_quality,
		(server->fake_width ? server->fake_width : EXPOSED(picture->width)),
		(server->fake_height ? server->fake_height : EXPOSED(picture->height)),
		bool_to_string(EXPOSED(online)),
		server->run->stream->dev->desired_fps,
		EXPOSED(captured_fps),
		EXPOSED(queued_fps),
		server->run->stream_clients_count
	));

#	undef EXPOSED
#	undef ENCODER

	for (struct stream_client_t * client = server->run->stream_clients; client != NULL; client = client->next) {
		assert(evbuffer_add_printf(buf,
			"\"%s\": {\"fps\": %u, \"extra_headers\": %s, \"advance_headers\": %s, \"dual_final_frames\": %s}%s",
			client->id,
			client->fps,
			bool_to_string(client->extra_headers),
			bool_to_string(client->advance_headers),
			bool_to_string(client->dual_final_frames),
			(client->next ? ", " : "")
		));
	}

	assert(evbuffer_add_printf(buf, "}}}}"));

	ADD_HEADER("Content-Type", "application/json");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_snapshot(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;
	char header_buf[64];

	PREPROCESS_REQUEST;

#	define EXPOSED(_next) server->run->exposed->_next

	assert((buf = evbuffer_new()));
	assert(!evbuffer_add(buf, (const void *)EXPOSED(picture->data), EXPOSED(picture->used)));

	ADD_HEADER("Access-Control-Allow-Origin:", "*");
	ADD_HEADER("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate, pre-check=0, post-check=0, max-age=0");
	ADD_HEADER("Pragma", "no-cache");
	ADD_HEADER("Expires", "Mon, 3 Jan 2000 12:34:56 GMT");

#	define ADD_TIME_HEADER(_key, _value) { \
			sprintf(header_buf, "%.06Lf", _value); \
			ADD_HEADER(_key, header_buf); \
		}

#	define ADD_UNSIGNED_HEADER(_key, _value) { \
			sprintf(header_buf, "%u", _value); \
			ADD_HEADER(_key, header_buf); \
		}

	ADD_TIME_HEADER("X-Timestamp", get_now_real());

	ADD_HEADER("X-UStreamer-Online",						bool_to_string(EXPOSED(online)));
	ADD_UNSIGNED_HEADER("X-UStreamer-Dropped",				EXPOSED(dropped));
	ADD_UNSIGNED_HEADER("X-UStreamer-Width",				EXPOSED(picture->width));
	ADD_UNSIGNED_HEADER("X-UStreamer-Height",				EXPOSED(picture->height));
	ADD_TIME_HEADER("X-UStreamer-Grab-Timestamp",			EXPOSED(picture->grab_ts));
	ADD_TIME_HEADER("X-UStreamer-Encode-Begin-Timestamp",	EXPOSED(picture->encode_begin_ts));
	ADD_TIME_HEADER("X-UStreamer-Encode-End-Timestamp",		EXPOSED(picture->encode_end_ts));
	ADD_TIME_HEADER("X-UStreamer-Expose-Begin-Timestamp",	EXPOSED(expose_begin_ts));
	ADD_TIME_HEADER("X-UStreamer-Expose-Cmp-Timestamp",		EXPOSED(expose_cmp_ts));
	ADD_TIME_HEADER("X-UStreamer-Expose-End-Timestamp",		EXPOSED(expose_end_ts));
	ADD_TIME_HEADER("X-UStreamer-Send-Timestamp",			get_now_monotonic());

#	undef ADD_UNSUGNED_HEADER
#	undef ADD_TIME_HEADER
#	undef EXPOSED

	ADD_HEADER("Content-Type", "image/jpeg");

	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
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
	struct evkeyvalq params;
	struct bufferevent *buf_event;
	struct stream_client_t *client;
	char *client_addr;
	unsigned short client_port;
	uuid_t uuid;

	PREPROCESS_REQUEST;

	conn = evhttp_request_get_connection(request);
	if (conn) {
		A_CALLOC(client, 1);
		client->server = server;
		client->request = request;
		client->need_initial = true;
		client->need_first_frame = true;

		evhttp_parse_query(evhttp_request_get_uri(request), &params);
		client->key = uri_get_string(&params, "key");
		client->extra_headers = uri_get_true(&params, "extra_headers");
		client->advance_headers = uri_get_true(&params, "advance_headers");
		client->dual_final_frames = uri_get_true(&params, "dual_final_frames");
		evhttp_clear_headers(&params);

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

		if (server->run->stream_clients_count == 1) {
			if (server->slowdown) {
				stream_switch_slowdown(server->run->stream, false);
			}

#			ifdef WITH_GPIO
			GPIO_SET_HIGH(has_http_clients);
#			endif
		}

		evhttp_connection_get_peer(conn, &client_addr, &client_port);
		LOG_INFO("HTTP: Registered client: [%s]:%u, id=%s; clients now: %u",
			client_addr, client_port, client->id, server->run->stream_clients_count);

		buf_event = evhttp_connection_get_bufferevent(conn);
		if (server->tcp_nodelay && !server->run->unix_fd) {
			evutil_socket_t fd;
			int on = 1;

			LOG_DEBUG("HTTP: Setting up TCP_NODELAY to the client [%s]:%u ...", client_addr, client_port);
			assert((fd = bufferevent_getfd(buf_event)) >= 0);
			if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) != 0) {
				LOG_PERROR("HTTP: Can't set TCP_NODELAY to the client [%s]:%u", client_addr, client_port);
			}
		}
		bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
		bufferevent_enable(buf_event, EV_READ);
	} else {
		evhttp_request_free(request);
	}
}

#undef PREPROCESS_REQUEST

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

	// В хроме и его производных есть фундаментальный баг: он отрисовывает
	// фрейм с задержкой на один, как только ему придут заголовки следующего.
	// В сочетании с drop_same_frames это дает значительный лаг стрима
	// при большом количестве дропов (на статичном изображении, где внезапно
	// что-то изменилось.
	//
	// https://bugs.chromium.org/p/chromium/issues/detail?id=527446
	//
	// Включение advance_headers заставляет стример отсылать заголовки
	// будущего фрейма сразу после данных текущего, чтобы триггернуть отрисовку.
	// Естественным следствием этого является невозможность установки заголовка
	// Content-Length, так как предсказывать будущее мы еще не научились.
	// Его наличие не требуется RFC, однако никаких стандартов на MJPG over HTTP
	// в природе не существует, и никто не может гарантировать, что отсутствие
	// Content-Length не сломает вещание для каких-нибудь маргинальных браузеров.
	//
	// Кроме того, advance_headers форсит отключение заголовков X-UStreamer-*
	// по тем же причинам, по которым у нас нет Content-Length.

#	define ADD_ADVANCE_HEADERS \
		assert(evbuffer_add_printf(buf, \
			"Content-Type: image/jpeg" RN "X-Timestamp: %.06Lf" RN RN, get_now_real()))

	if (client->need_initial) {
		assert(evbuffer_add_printf(buf,
			"HTTP/1.0 200 OK" RN
			"Access-Control-Allow-Origin: *" RN
			"Cache-Control: no-store, no-cache, must-revalidate, proxy-revalidate, pre-check=0, post-check=0, max-age=0" RN
			"Pragma: no-cache" RN
			"Expires: Mon, 3 Jan 2000 12:34:56 GMT" RN
			"Set-Cookie: stream_client=%s/%s; path=/; max-age=30" RN
			"Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY RN
			RN
			"--" BOUNDARY RN,
			(client->key != NULL ? client->key : "0"),
			client->id
		));

		if (client->advance_headers) {
			ADD_ADVANCE_HEADERS;
		}

		assert(!bufferevent_write_buffer(buf_event, buf));
		client->need_initial = false;
	}

#	define EXPOSED(_next) client->server->run->exposed->_next

	if (!client->advance_headers) {
		assert(evbuffer_add_printf(buf,
			"Content-Type: image/jpeg" RN
			"Content-Length: %zu" RN
			"X-Timestamp: %.06Lf" RN
			"%s",
			EXPOSED(picture->used),
			get_now_real(),
			(client->extra_headers ? "" : RN)
		));
		if (client->extra_headers) {
			assert(evbuffer_add_printf(buf,
				"X-UStreamer-Online: %s" RN
				"X-UStreamer-Dropped: %u" RN
				"X-UStreamer-Width: %u" RN
				"X-UStreamer-Height: %u" RN
				"X-UStreamer-Client-FPS: %u" RN
				"X-UStreamer-Grab-Time: %.06Lf" RN
				"X-UStreamer-Encode-Begin-Time: %.06Lf" RN
				"X-UStreamer-Encode-End-Time: %.06Lf" RN
				"X-UStreamer-Expose-Begin-Time: %.06Lf" RN
				"X-UStreamer-Expose-Cmp-Time: %.06Lf" RN
				"X-UStreamer-Expose-End-Time: %.06Lf" RN
				"X-UStreamer-Send-Time: %.06Lf" RN
				RN,
				bool_to_string(EXPOSED(online)),
				EXPOSED(dropped),
				EXPOSED(picture->width),
				EXPOSED(picture->height),
				client->fps,
				EXPOSED(picture->grab_ts),
				EXPOSED(picture->encode_begin_ts),
				EXPOSED(picture->encode_end_ts),
				EXPOSED(expose_begin_ts),
				EXPOSED(expose_cmp_ts),
				EXPOSED(expose_end_ts),
				now
			));
		}
	}

	assert(!evbuffer_add(buf, (void *)EXPOSED(picture->data), EXPOSED(picture->used)));
	assert(evbuffer_add_printf(buf, RN "--" BOUNDARY RN));

	if (client->advance_headers) {
		ADD_ADVANCE_HEADERS;
	}

	assert(!bufferevent_write_buffer(buf_event, buf));
	evbuffer_free(buf);

	bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
	bufferevent_enable(buf_event, EV_READ);

#	undef EXPOSED
#	undef ADD_ADVANCE_HEADERS
#	undef RN
#	undef BOUNDARY
}

static void _http_callback_stream_error(UNUSED struct bufferevent *buf_event, UNUSED short what, void *v_client) {
	struct stream_client_t *client = (struct stream_client_t *)v_client;
	struct evhttp_connection *conn;
	char *client_addr = "???";
	unsigned short client_port = 0;
	char reason[2048] = {0};

	_format_bufferevent_reason(what, reason);

#	define RUN(_next) client->server->run->_next

	assert(RUN(stream_clients_count) > 0);
	RUN(stream_clients_count) -= 1;

	if (RUN(stream_clients_count) == 0) {
		if (client->server->slowdown) {
			stream_switch_slowdown(RUN(stream), true);
		}

#		ifdef WITH_GPIO
		GPIO_SET_LOW(has_http_clients);
#		endif
	}

	conn = evhttp_request_get_connection(client->request);
	if (conn) {
		evhttp_connection_get_peer(conn, &client_addr, &client_port);
	}

	LOG_INFO("HTTP: Disconnected client: [%s]:%u, id=%s, %s; clients now: %u",
		client_addr, client_port, client->id, reason, RUN(stream_clients_count));
	if (conn) {
		evhttp_connection_free(conn);
	}

	if (client->prev == NULL) {
		RUN(stream_clients) = client->next;
	} else {
		client->prev->next = client->next;
	}
	if (client->next != NULL) {
		client->next->prev = client->prev;
	}
	free(client->key);
	free(client);

#	undef RUN
}

static void _http_queue_send_stream(struct http_server_t *server, bool stream_updated, bool picture_updated) {
	struct evhttp_connection *conn;
	struct bufferevent *buf_event;
	long long now;
	bool has_clients = false;
	bool queued = false;

	for (struct stream_client_t *client = server->run->stream_clients; client != NULL; client = client->next) {
		conn = evhttp_request_get_connection(client->request);
		if (conn) {
			// Фикс для бага WebKit. При включенной опции дропа одинаковых фреймов,
			// WebKit отрисовывает последний фрейм в серии с некоторой задержкой,
			// и нужно послать два фрейма, чтобы серия была вовремя завершена.
			// Это похоже на баг Blink (см. _http_callback_stream_write() и advance_headers),
			// но фикс для него не лечит проблему вебкита. Такие дела.

			bool dual_update = (
				server->drop_same_frames
				&& client->dual_final_frames
				&& stream_updated
				&& client->updated_prev
				&& !picture_updated
			);

			if (dual_update || picture_updated || client->need_first_frame) {
				buf_event = evhttp_connection_get_bufferevent(conn);
				bufferevent_setcb(buf_event, NULL, _http_callback_stream_write, _http_callback_stream_error, (void *)client);
				bufferevent_enable(buf_event, EV_READ|EV_WRITE);

				client->need_first_frame = false;
				client->updated_prev = (picture_updated || client->need_first_frame); // Игнорировать dual
				queued = true;
			} else if (stream_updated) { // Для dual
				client->updated_prev = false;
			}

			has_clients = true;
		}
	}

	if (queued) {
		static unsigned queued_fps_accum = 0;
		static long long queued_fps_second = 0;

		if ((now = floor_ms(get_now_monotonic())) != queued_fps_second) {
			server->run->exposed->queued_fps = queued_fps_accum;
			queued_fps_accum = 0;
			queued_fps_second = now;
		}
		queued_fps_accum += 1;
	} else if (!has_clients) {
		server->run->exposed->queued_fps = 0;
	}
}

static void _http_exposed_refresh(UNUSED int fd, UNUSED short what, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	bool stream_updated = false;
	bool picture_updated = false;

#	define UNLOCK_STREAM { \
			atomic_store(&server->run->stream->updated, false); \
			A_MUTEX_UNLOCK(&server->run->stream->mutex); \
		}

	if (atomic_load(&server->run->stream->updated)) {
		LOG_DEBUG("Refreshing HTTP exposed ...");
		A_MUTEX_LOCK(&server->run->stream->mutex);
		if (server->run->stream->online) {
			picture_updated = _expose_new_picture_unsafe(server);
			UNLOCK_STREAM;
		} else {
			UNLOCK_STREAM;
			picture_updated = _expose_blank_picture(server);
		}
		stream_updated = true;
	} else if (!server->run->exposed->online) {
		LOG_DEBUG("Refreshing HTTP exposed (BLANK) ...");
		picture_updated = _expose_blank_picture(server);
		stream_updated = true;
	}

#	undef UNLOCK_STREAM

	_http_queue_send_stream(server, stream_updated, picture_updated);

#	define EXPOSED(_next) server->run->exposed->_next

	if (
		picture_updated
		&& server->notify_parent
		&& (
			EXPOSED(notify_last_online) != EXPOSED(online)
			|| EXPOSED(notify_last_width) != EXPOSED(picture->width)
			|| EXPOSED(notify_last_height) != EXPOSED(picture->height)
		)
	) {
		EXPOSED(notify_last_online) = EXPOSED(online);
		EXPOSED(notify_last_width) = EXPOSED(picture->width);
		EXPOSED(notify_last_height) = EXPOSED(picture->height);
		process_notify_parent();
	}

#	undef EXPOSED
}

static bool _expose_new_picture_unsafe(struct http_server_t *server) {
#	define EXPOSED(_next) server->run->exposed->_next
#	define STREAM(_next) server->run->stream->_next

	EXPOSED(captured_fps) = STREAM(captured_fps);
	EXPOSED(expose_begin_ts) = get_now_monotonic();

	if (server->drop_same_frames) {
		if (
			EXPOSED(online)
			&& EXPOSED(dropped) < server->drop_same_frames
			&& picture_compare(EXPOSED(picture), STREAM(picture))
		) {
			EXPOSED(expose_cmp_ts) = get_now_monotonic();
			EXPOSED(expose_end_ts) = EXPOSED(expose_cmp_ts);
			LOG_VERBOSE("HTTP: Dropped same frame number %u; cmp_time=%.06Lf",
				EXPOSED(dropped), EXPOSED(expose_cmp_ts) - EXPOSED(expose_begin_ts));
			EXPOSED(dropped) += 1;
			return false; // Not updated
		} else {
			EXPOSED(expose_cmp_ts) = get_now_monotonic();
			LOG_VERBOSE("HTTP: Passed same frame check (frames are differ); cmp_time=%.06Lf",
				EXPOSED(expose_cmp_ts) - EXPOSED(expose_begin_ts));
		}
	}

	picture_copy(STREAM(picture), EXPOSED(picture));

#	undef STREAM

	EXPOSED(online) = true;
	EXPOSED(dropped) = 0;
	EXPOSED(expose_cmp_ts) = EXPOSED(expose_begin_ts);
	EXPOSED(expose_end_ts) = get_now_monotonic();

	LOG_VERBOSE("HTTP: Exposed new frame; full exposition time = %.06Lf",
		 EXPOSED(expose_end_ts) - EXPOSED(expose_begin_ts));

#	undef EXPOSED
	return true; // Updated
}

static bool _expose_blank_picture(struct http_server_t *server) {
#	define EXPOSED(_next) server->run->exposed->_next

	EXPOSED(expose_begin_ts) = get_now_monotonic();
	EXPOSED(expose_cmp_ts) = EXPOSED(expose_begin_ts);

#	define EXPOSE_BLANK picture_copy(server->run->blank, EXPOSED(picture))

	if (EXPOSED(online)) { // Если переходим из online в offline
		if (server->last_as_blank < 0) { // Если last_as_blank выключено, просто покажем картинку
			LOG_INFO("HTTP: Changed picture to BLANK");
			EXPOSE_BLANK;
		} else if (server->last_as_blank > 0) { // Если нужен таймер - запустим
			LOG_INFO("HTTP: Freezing last alive frame for %d seconds", server->last_as_blank);
			EXPOSED(last_as_blank_ts) = get_now_monotonic();
		} else { // last_as_blank == 0 - показываем последний фрейм вечно
			LOG_INFO("HTTP: Freezing last alive frame forever");
		}
		goto updated;
	}

	if ( // Если уже оффлайн, включена фича last_as_blank с таймером и он запущен
		server->last_as_blank > 0
		&& EXPOSED(last_as_blank_ts) > 0
		&& EXPOSED(last_as_blank_ts) + server->last_as_blank < EXPOSED(expose_begin_ts)
	) {
		LOG_INFO("HTTP: Changed last alive frame to BLANK");
		EXPOSE_BLANK;
		EXPOSED(last_as_blank_ts) = 0; // Останавливаем таймер
		goto updated;
	}

#	undef EXPOSE_BLANK

	if (EXPOSED(dropped) < server->run->drop_same_frames_blank) {
		LOG_PERF("HTTP: Dropped same frame (BLANK) number %u", EXPOSED(dropped));
		EXPOSED(dropped) += 1;
		EXPOSED(expose_end_ts) = get_now_monotonic();
		return false; // Not updated
	}

	updated:
		EXPOSED(captured_fps) = 0;
		EXPOSED(online) = false;
		EXPOSED(dropped) = 0;
		EXPOSED(expose_end_ts) = get_now_monotonic();
		return true; // Updated

#	undef EXPOSED
}

static void _format_bufferevent_reason(short what, char *reason) {
	char perror_buf[1024] = {0};
	char *perror_ptr = errno_to_string(EVUTIL_SOCKET_ERROR(), perror_buf, 1024); // evutil_socket_error_to_string() is not thread-sage
	bool first = true;

	strcat(reason, perror_ptr);
	strcat(reason, " (");

#	define FILL_REASON(_bev, _name) { \
			if (what & _bev) { \
				if (first) { \
					first = false; \
				} else { \
					strcat(reason, ","); \
				} \
				strcat(reason, _name); \
			} \
		}

	FILL_REASON(BEV_EVENT_READING, "reading");
	FILL_REASON(BEV_EVENT_WRITING, "writing");
	FILL_REASON(BEV_EVENT_ERROR, "error");
	FILL_REASON(BEV_EVENT_TIMEOUT, "timeout");
	FILL_REASON(BEV_EVENT_EOF, "eof"); // cppcheck-suppress unreadVariable

#	undef FILL_REASON

	strcat(reason, ")");
}
