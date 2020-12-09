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


static int _http_preprocess_request(struct evhttp_request *request, struct http_server_t *server);

static void _http_callback_root(struct evhttp_request *request, void *v_server);
static void _http_callback_static(struct evhttp_request *request, void *v_server);
static void _http_callback_state(struct evhttp_request *request, void *v_server);
static void _http_callback_snapshot(struct evhttp_request *request, void *v_server);

static void _http_callback_stream(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_ctx);
static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_ctx);

static void _http_exposed_refresh(int fd, short event, void *v_server);
static void _http_queue_send_stream(struct http_server_t *server, bool stream_updated, bool frame_updated);

static bool _expose_new_frame(struct http_server_t *server);

static void _format_bufferevent_reason(short what, char *reason);


#define RUN(_next)		server->run->_next
#define STREAM(_next)	RUN(stream->_next)
#define EX(_next)		RUN(exposed->_next)


struct http_server_t *http_server_init(struct stream_t *stream) {
	struct http_server_runtime_t *run;
	struct http_server_t *server;
	struct exposed_t *exposed;

	A_CALLOC(exposed, 1);
	exposed->frame = frame_init("http_exposed");

	A_CALLOC(run, 1);
	run->stream = stream;
	run->exposed = exposed;

	A_CALLOC(server, 1);
	server->host = "127.0.0.1";
	server->port = 8080;
	server->unix_path = "";
	server->user = "";
	server->passwd = "";
	server->static_path = "";
	server->allow_origin = "";
	server->timeout = 10;
	server->run = run;

	assert(!evthread_use_pthreads());
	assert((run->base = event_base_new()));
	assert((run->http = evhttp_new(run->base)));
	evhttp_set_allowed_methods(run->http, EVHTTP_REQ_GET|EVHTTP_REQ_HEAD);
	return server;
}

void http_server_destroy(struct http_server_t *server) {
	if (RUN(refresh)) {
		event_del(RUN(refresh));
		event_free(RUN(refresh));
	}

	evhttp_free(RUN(http));
	if (RUN(unix_fd)) {
		close(RUN(unix_fd));
	}
	event_base_free(RUN(base));

#	if LIBEVENT_VERSION_NUMBER >= 0x02010100
	libevent_global_shutdown();
#	endif

	for (struct stream_client_t *client = RUN(stream_clients); client != NULL;) {
		struct stream_client_t *next = client->next;

		free(client->key);
		free(client);
		client = next;
	}

	if (RUN(auth_token)) {
		free(RUN(auth_token));
	}

	frame_destroy(EX(frame));
	free(RUN(exposed));
	free(server->run);
	free(server);
}

int http_server_listen(struct http_server_t *server) {
	{
		if (server->static_path[0] != '\0') {
			LOG_INFO("Enabling HTTP file server: %s", server->static_path);
			evhttp_set_gencb(RUN(http), _http_callback_static, (void *)server);
		} else {
			assert(!evhttp_set_cb(RUN(http), "/", _http_callback_root, (void *)server));
		}
		assert(!evhttp_set_cb(RUN(http), "/state", _http_callback_state, (void *)server));
		assert(!evhttp_set_cb(RUN(http), "/snapshot", _http_callback_snapshot, (void *)server));
		assert(!evhttp_set_cb(RUN(http), "/stream", _http_callback_stream, (void *)server));
	}

	frame_copy(STREAM(blank), EX(frame));
	EX(expose_begin_ts) = 0;
	EX(expose_cmp_ts) = 0;
	EX(expose_end_ts) = 0;
	EX(notify_last_width) = EX(frame->width);
	EX(notify_last_height) = EX(frame->height);

	{
		struct timeval refresh_interval;

		refresh_interval.tv_sec = 0;
		if (STREAM(dev->desired_fps) > 0) {
			refresh_interval.tv_usec = 1000000 / (STREAM(dev->desired_fps) * 2);
		} else {
			refresh_interval.tv_usec = 16000; // ~60fps
		}

		assert((RUN(refresh) = event_new(RUN(base), -1, EV_PERSIST, _http_exposed_refresh, server)));
		assert(!event_add(RUN(refresh), &refresh_interval));
	}

	if (server->slowdown) {
		stream_switch_slowdown(RUN(stream), true);
	}

	evhttp_set_timeout(RUN(http), server->timeout);

	if (server->user[0] != '\0') {
		char *raw_token;
		char *encoded_token;

		A_CALLOC(raw_token, strlen(server->user) + strlen(server->passwd) + 2);
		sprintf(raw_token, "%s:%s", server->user, server->passwd);
		encoded_token = base64_encode((unsigned char *)raw_token);
		free(raw_token);

		A_CALLOC(RUN(auth_token), strlen(encoded_token) + 16);
		sprintf(RUN(auth_token), "Basic %s", encoded_token);
		free(encoded_token);

		LOG_INFO("Using HTTP basic auth");
	}

	if (server->unix_path[0] != '\0') {
		LOG_DEBUG("Binding HTTP to UNIX socket '%s' ...", server->unix_path);
		if ((RUN(unix_fd) = evhttp_my_bind_unix(
			RUN(http),
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
		if (evhttp_bind_socket(RUN(http), server->host, server->port) < 0) {
			LOG_PERROR("Can't bind HTTP on [%s]:%u", server->host, server->port)
			return -1;
		}
		LOG_INFO("Listening HTTP on [%s]:%u", server->host, server->port);
	}

	return 0;
}

void http_server_loop(struct http_server_t *server) {
	LOG_INFO("Starting HTTP eventloop ...");
	event_base_dispatch(RUN(base));
	LOG_INFO("HTTP eventloop stopped");
}

void http_server_loop_break(struct http_server_t *server) {
	event_base_loopbreak(RUN(base));
}

#define ADD_HEADER(_key, _value) \
	assert(!evhttp_add_header(evhttp_request_get_output_headers(request), _key, _value))

static int _http_preprocess_request(struct evhttp_request *request, struct http_server_t *server) {
	if (RUN(auth_token)) {
		const char *token = evhttp_find_header(evhttp_request_get_input_headers(request), "Authorization");

		if (token == NULL || strcmp(token, RUN(auth_token)) != 0) {
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
	struct evkeyvalq params; // For mjpg-streamer compatibility
	const char *action; // Ditto

	PREPROCESS_REQUEST;

	evhttp_parse_query(evhttp_request_get_uri(request), &params);
	action = evhttp_find_header(&params, "action");

	if (action && !strcmp(action, "snapshot")) {
		_http_callback_snapshot(request, v_server);
	} else if (action && !strcmp(action, "stream")) {
		_http_callback_stream(request, v_server);
	} else {
		assert((buf = evbuffer_new()));
		assert(evbuffer_add_printf(buf, "%s", HTML_INDEX_PAGE));
		ADD_HEADER("Content-Type", "text/html");
		evhttp_send_reply(request, HTTP_OK, "OK", buf);
		evbuffer_free(buf);
	}

	evhttp_clear_headers(&params);
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
	enum encoder_type_t encoder_type;
	unsigned encoder_quality;

	PREPROCESS_REQUEST;

	encoder_get_runtime_params(STREAM(encoder), &encoder_type, &encoder_quality);

	assert((buf = evbuffer_new()));

	assert(evbuffer_add_printf(buf,
		"{\"ok\": true, \"result\": {"
		" \"encoder\": {\"type\": \"%s\", \"quality\": %u},"
		" \"source\": {\"resolution\": {\"width\": %u, \"height\": %u},"
		" \"online\": %s, \"desired_fps\": %u, \"captured_fps\": %u},"
		" \"stream\": {\"queued_fps\": %u, \"clients\": %u, \"clients_stat\": {",
		encoder_type_to_string(encoder_type),
		encoder_quality,
		(server->fake_width ? server->fake_width : EX(frame->width)),
		(server->fake_height ? server->fake_height : EX(frame->height)),
		bool_to_string(EX(online)),
		STREAM(dev->desired_fps),
		EX(captured_fps),
		EX(queued_fps),
		RUN(stream_clients_count)
	));

	for (struct stream_client_t * client = RUN(stream_clients); client != NULL; client = client->next) {
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

	assert((buf = evbuffer_new()));
	assert(!evbuffer_add(buf, (const void *)EX(frame->data), EX(frame->used)));

	if (server->allow_origin[0] != '\0') {
		ADD_HEADER("Access-Control-Allow-Origin", server->allow_origin);
	}
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

	ADD_HEADER("X-UStreamer-Online",						bool_to_string(EX(online)));
	ADD_UNSIGNED_HEADER("X-UStreamer-Dropped",				EX(dropped));
	ADD_UNSIGNED_HEADER("X-UStreamer-Width",				EX(frame->width));
	ADD_UNSIGNED_HEADER("X-UStreamer-Height",				EX(frame->height));
	ADD_TIME_HEADER("X-UStreamer-Grab-Timestamp",			EX(frame->grab_ts));
	ADD_TIME_HEADER("X-UStreamer-Encode-Begin-Timestamp",	EX(frame->encode_begin_ts));
	ADD_TIME_HEADER("X-UStreamer-Encode-End-Timestamp",		EX(frame->encode_end_ts));
	ADD_TIME_HEADER("X-UStreamer-Expose-Begin-Timestamp",	EX(expose_begin_ts));
	ADD_TIME_HEADER("X-UStreamer-Expose-Cmp-Timestamp",		EX(expose_cmp_ts));
	ADD_TIME_HEADER("X-UStreamer-Expose-End-Timestamp",		EX(expose_end_ts));
	ADD_TIME_HEADER("X-UStreamer-Send-Timestamp",			get_now_monotonic());

#	undef ADD_UNSUGNED_HEADER
#	undef ADD_TIME_HEADER

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

		if (RUN(stream_clients) == NULL) {
			RUN(stream_clients) = client;
		} else {
			struct stream_client_t *last = RUN(stream_clients);

			for (; last->next != NULL; last = last->next);
			client->prev = last;
			last->next = client;
		}
		RUN(stream_clients_count) += 1;

		if (RUN(stream_clients_count) == 1) {
			if (server->slowdown) {
				stream_switch_slowdown(RUN(stream), false);
			}

#			ifdef WITH_GPIO
			gpio_set_has_http_clients(true);
#			endif
		}

		evhttp_connection_get_peer(conn, &client_addr, &client_port);
		LOG_INFO("HTTP: Registered client: [%s]:%u, id=%s; clients now: %u",
			client_addr, client_port, client->id, RUN(stream_clients_count));

		buf_event = evhttp_connection_get_bufferevent(conn);
		if (server->tcp_nodelay && !RUN(unix_fd)) {
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
	struct http_server_t *server = client->server;
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
		assert(evbuffer_add_printf(buf, "HTTP/1.0 200 OK" RN));
		if (client->server->allow_origin[0] != '\0') {
			assert(evbuffer_add_printf(buf, "Access-Control-Allow-Origin: %s" RN, client->server->allow_origin));
		}
		assert(evbuffer_add_printf(buf,
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

	if (!client->advance_headers) {
		assert(evbuffer_add_printf(buf,
			"Content-Type: image/jpeg" RN
			"Content-Length: %zu" RN
			"X-Timestamp: %.06Lf" RN
			"%s",
			EX(frame->used),
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
				bool_to_string(EX(online)),
				EX(dropped),
				EX(frame->width),
				EX(frame->height),
				client->fps,
				EX(frame->grab_ts),
				EX(frame->encode_begin_ts),
				EX(frame->encode_end_ts),
				EX(expose_begin_ts),
				EX(expose_cmp_ts),
				EX(expose_end_ts),
				now
			));
		}
	}

	assert(!evbuffer_add(buf, (void *)EX(frame->data), EX(frame->used)));
	assert(evbuffer_add_printf(buf, RN "--" BOUNDARY RN));

	if (client->advance_headers) {
		ADD_ADVANCE_HEADERS;
	}

	assert(!bufferevent_write_buffer(buf_event, buf));
	evbuffer_free(buf);

	bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
	bufferevent_enable(buf_event, EV_READ);

#	undef ADD_ADVANCE_HEADERS
#	undef RN
#	undef BOUNDARY
}

static void _http_callback_stream_error(UNUSED struct bufferevent *buf_event, UNUSED short what, void *v_client) {
	struct stream_client_t *client = (struct stream_client_t *)v_client;
	struct http_server_t *server = client->server;
	struct evhttp_connection *conn;
	char *client_addr = "???";
	unsigned short client_port = 0;
	char reason[2048] = {0};

	_format_bufferevent_reason(what, reason);

	assert(RUN(stream_clients_count) > 0);
	RUN(stream_clients_count) -= 1;

	if (RUN(stream_clients_count) == 0) {
		if (client->server->slowdown) {
			stream_switch_slowdown(RUN(stream), true);
		}

#		ifdef WITH_GPIO
		gpio_set_has_http_clients(false);
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
}

static void _http_queue_send_stream(struct http_server_t *server, bool stream_updated, bool frame_updated) {
	struct evhttp_connection *conn;
	struct bufferevent *buf_event;
	long long now;
	bool has_clients = false;
	bool queued = false;

	for (struct stream_client_t *client = RUN(stream_clients); client != NULL; client = client->next) {
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
				&& !frame_updated
			);

			if (dual_update || frame_updated || client->need_first_frame) {
				buf_event = evhttp_connection_get_bufferevent(conn);
				bufferevent_setcb(buf_event, NULL, _http_callback_stream_write, _http_callback_stream_error, (void *)client);
				bufferevent_enable(buf_event, EV_READ|EV_WRITE);

				client->need_first_frame = false;
				client->updated_prev = (frame_updated || client->need_first_frame); // Игнорировать dual
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
			EX(queued_fps) = queued_fps_accum;
			queued_fps_accum = 0;
			queued_fps_second = now;
		}
		queued_fps_accum += 1;
	} else if (!has_clients) {
		EX(queued_fps) = 0;
	}
}

static void _http_exposed_refresh(UNUSED int fd, UNUSED short what, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	bool stream_updated = false;
	bool frame_updated = false;

	if (atomic_load(&STREAM(video->updated))) {
		frame_updated = _expose_new_frame(server);
		stream_updated = true;
	} else if (EX(expose_end_ts) + 1 < get_now_monotonic()) {
		LOG_DEBUG("HTTP: Repeating exposed ...");
		EX(expose_begin_ts) = get_now_monotonic();
		EX(expose_cmp_ts) = EX(expose_begin_ts);
		EX(expose_end_ts) = EX(expose_begin_ts);
		frame_updated = true;
		stream_updated = true;
	}

	_http_queue_send_stream(server, stream_updated, frame_updated);

	if (
		frame_updated
		&& server->notify_parent
		&& (
			EX(notify_last_online) != EX(online)
			|| EX(notify_last_width) != EX(frame->width)
			|| EX(notify_last_height) != EX(frame->height)
		)
	) {
		EX(notify_last_online) = EX(online);
		EX(notify_last_width) = EX(frame->width);
		EX(notify_last_height) = EX(frame->height);
		process_notify_parent();
	}
}

static bool _expose_new_frame(struct http_server_t *server) {
	bool updated = false;

	A_MUTEX_LOCK(&STREAM(video->mutex));

	LOG_DEBUG("HTTP: Updating exposed frame (online=%d) ...", STREAM(video->online));

	EX(captured_fps) = STREAM(video->captured_fps);
	EX(expose_begin_ts) = get_now_monotonic();

	if (server->drop_same_frames && STREAM(video->online)) {
		bool need_drop = false;
		bool maybe_same = false;
		if (
			(need_drop = (EX(dropped) < server->drop_same_frames))
			&& (maybe_same = frame_compare(EX(frame), STREAM(video->frame)))
		) {
			EX(expose_cmp_ts) = get_now_monotonic();
			EX(expose_end_ts) = EX(expose_cmp_ts);
			LOG_VERBOSE("HTTP: Dropped same frame number %u; cmp_time=%.06Lf",
				EX(dropped), EX(expose_cmp_ts) - EX(expose_begin_ts));
			EX(dropped) += 1;
			goto not_updated;
		} else {
			EX(expose_cmp_ts) = get_now_monotonic();
			LOG_VERBOSE("HTTP: Passed same frame check (need_drop=%d, maybe_same=%d); cmp_time=%.06Lf",
				need_drop, maybe_same, (EX(expose_cmp_ts) - EX(expose_begin_ts)));
		}
	}

	frame_copy(STREAM(video->frame), EX(frame));

	EX(online) = STREAM(video->online);
	EX(dropped) = 0;
	EX(expose_cmp_ts) = EX(expose_begin_ts);
	EX(expose_end_ts) = get_now_monotonic();

	LOG_VERBOSE("HTTP: Exposed frame: online=%d, exp_time=%.06Lf",
		 EX(online), EX(expose_end_ts) - EX(expose_begin_ts));

	updated = true;
	not_updated:
		atomic_store(&STREAM(video->updated), false);
		A_MUTEX_UNLOCK(&STREAM(video->mutex));
		return updated;
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

#undef EX
#undef STREAM
#undef RUN
