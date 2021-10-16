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


#include "server.h"


static int _http_preprocess_request(struct evhttp_request *request, server_s *server);

static int _http_check_run_compat_action(struct evhttp_request *request, void *v_server);

static void _http_callback_root(struct evhttp_request *request, void *v_server);
static void _http_callback_static(struct evhttp_request *request, void *v_server);
static void _http_callback_state(struct evhttp_request *request, void *v_server);
static void _http_callback_snapshot(struct evhttp_request *request, void *v_server);

static void _http_callback_stream(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_ctx);
static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_ctx);

static void _http_exposed_refresh(int fd, short event, void *v_server);
static void _http_queue_send_stream(server_s *server, bool stream_updated, bool frame_updated);

static bool _expose_new_frame(server_s *server);

static char *_http_get_client_hostport(struct evhttp_request *request);


#define RUN(_next)		server->run->_next
#define STREAM(_next)	RUN(stream->_next)
#define VID(_next)		STREAM(run->video->_next)
#define EX(_next)		RUN(exposed->_next)


server_s *server_init(stream_s *stream) {
	exposed_s *exposed;
	A_CALLOC(exposed, 1);
	exposed->frame = frame_init();

	server_runtime_s *run;
	A_CALLOC(run, 1);
	run->stream = stream;
	run->exposed = exposed;

	server_s *server;
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

void server_destroy(server_s *server) {
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

	LIST_ITERATE(RUN(stream_clients), client, {
		free(client->key);
		free(client->hostport);
		free(client);
	});

	if (RUN(auth_token)) {
		free(RUN(auth_token));
	}

	frame_destroy(EX(frame));
	free(RUN(exposed));
	free(server->run);
	free(server);
}

int server_listen(server_s *server) {
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

	evhttp_set_timeout(RUN(http), server->timeout);

	if (server->user[0] != '\0') {
		char *encoded_token = NULL;

		char *raw_token;
		A_ASPRINTF(raw_token, "%s:%s", server->user, server->passwd);
		base64_encode((uint8_t *)raw_token, strlen(raw_token), &encoded_token, NULL);
		free(raw_token);

		A_ASPRINTF(RUN(auth_token), "Basic %s", encoded_token);
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

void server_loop(server_s *server) {
	LOG_INFO("Starting HTTP eventloop ...");
	event_base_dispatch(RUN(base));
	LOG_INFO("HTTP eventloop stopped");
}

void server_loop_break(server_s *server) {
	event_base_loopbreak(RUN(base));
}

#define ADD_HEADER(_key, _value) \
	assert(!evhttp_add_header(evhttp_request_get_output_headers(request), _key, _value))

static int _http_preprocess_request(struct evhttp_request *request, server_s *server) {
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

static int _http_check_run_compat_action(struct evhttp_request *request, void *v_server) {
	// MJPG-Streamer compatibility layer

	struct evkeyvalq params;
	int error = 0;

	evhttp_parse_query(evhttp_request_get_uri(request), &params);
	const char *action = evhttp_find_header(&params, "action");

	if (action && !strcmp(action, "snapshot")) {
		_http_callback_snapshot(request, v_server);
		goto ok;
	} else if (action && !strcmp(action, "stream")) {
		_http_callback_stream(request, v_server);
		goto ok;
	}

	error = -1;
	ok:
		evhttp_clear_headers(&params);
		return error;
}

#define COMPAT_REQUEST { \
		if (_http_check_run_compat_action(request, v_server) == 0) { \
			return; \
		} \
	}

static void _http_callback_root(struct evhttp_request *request, void *v_server) {
	server_s *server = (server_s *)v_server;

	PREPROCESS_REQUEST;
	COMPAT_REQUEST;

	struct evbuffer *buf;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf, "%s", HTML_INDEX_PAGE));
	ADD_HEADER("Content-Type", "text/html");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);

	evbuffer_free(buf);
}

static void _http_callback_static(struct evhttp_request *request, void *v_server) {
	server_s *server = (server_s *)v_server;

	PREPROCESS_REQUEST;
	COMPAT_REQUEST;

	struct evbuffer *buf = NULL;
	struct evhttp_uri *uri = NULL;
	char *decoded_path = NULL;
	char *static_path = NULL;
	int fd = -1;

	{
		char *uri_path;

		if ((uri = evhttp_uri_parse(evhttp_request_get_uri(request))) == NULL) {
			goto bad_request;
		}
		if ((uri_path = (char *)evhttp_uri_get_path(uri)) == NULL) {
			uri_path = "/";
		}

		if ((decoded_path = evhttp_uridecode(uri_path, 0, NULL)) == NULL) {
			goto bad_request;
		}
	}

	assert((buf = evbuffer_new()));

	if ((static_path = find_static_file_path(server->static_path, decoded_path)) == NULL) {
		goto not_found;
	}

	if ((fd = open(static_path, O_RDONLY)) < 0) {
		LOG_PERROR("HTTP: Can't open found static file %s", static_path);
		goto not_found;
	}

	{
		struct stat st;

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
	}

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

#undef COMPAT_REQUEST

static void _http_callback_state(struct evhttp_request *request, void *v_server) {
	server_s *server = (server_s *)v_server;

	PREPROCESS_REQUEST;

	encoder_type_e enc_type;
	unsigned enc_quality;
	encoder_get_runtime_params(STREAM(enc), &enc_type, &enc_quality);

	struct evbuffer *buf;
	assert((buf = evbuffer_new()));

	assert(evbuffer_add_printf(buf,
		"{\"ok\": true, \"result\": {"
		" \"encoder\": {\"type\": \"%s\", \"quality\": %u},",
		encoder_type_to_string(enc_type),
		enc_quality
	));

#	ifdef WITH_OMX
	if (STREAM(run->h264)) {
		assert(evbuffer_add_printf(buf,
			" \"h264\": {\"bitrate\": %u, \"gop\": %u, \"online\": %s},",
			STREAM(h264_bitrate),
			STREAM(h264_gop),
			bool_to_string(atomic_load(&STREAM(run->h264->online)))
		));
	}
#	endif

	if (
		STREAM(sink)
#	ifdef WITH_OMX
		|| STREAM(h264_sink)
#	endif
	) {
		assert(evbuffer_add_printf(buf, " \"sinks\": {"));
		if (STREAM(sink)) {
			assert(evbuffer_add_printf(buf,
				"\"jpeg\": {\"has_clients\": %s}",
				bool_to_string(atomic_load(&STREAM(sink->has_clients)))
			));
		}
#	ifdef WITH_OMX
		if (STREAM(h264_sink)) {
			assert(evbuffer_add_printf(buf,
				"%s\"h264\": {\"has_clients\": %s}",
				(STREAM(sink) ? ", " : ""),
				bool_to_string(atomic_load(&STREAM(h264_sink->has_clients)))
			));
		}
#	endif
		assert(evbuffer_add_printf(buf, "},"));
	}

	assert(evbuffer_add_printf(buf,
		" \"source\": {\"resolution\": {\"width\": %u, \"height\": %u},"
		" \"online\": %s, \"desired_fps\": %u, \"captured_fps\": %u},"
		" \"stream\": {\"queued_fps\": %u, \"clients\": %u, \"clients_stat\": {",
		(server->fake_width ? server->fake_width : EX(frame->width)),
		(server->fake_height ? server->fake_height : EX(frame->height)),
		bool_to_string(EX(frame->online)),
		STREAM(dev->desired_fps),
		EX(captured_fps),
		EX(queued_fps),
		RUN(stream_clients_count)
	));

	LIST_ITERATE(RUN(stream_clients), client, {
		assert(evbuffer_add_printf(buf,
			"\"%" PRIx64 "\": {\"fps\": %u, \"extra_headers\": %s, \"advance_headers\": %s,"
			" \"dual_final_frames\": %s, \"zero_data\": %s}%s",
			client->id,
			client->fps,
			bool_to_string(client->extra_headers),
			bool_to_string(client->advance_headers),
			bool_to_string(client->dual_final_frames),
			bool_to_string(client->zero_data),
			(client->next ? ", " : "")
		));
	});

	assert(evbuffer_add_printf(buf, "}}}}"));

	ADD_HEADER("Content-Type", "application/json");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_snapshot(struct evhttp_request *request, void *v_server) {
	server_s *server = (server_s *)v_server;

	PREPROCESS_REQUEST;

	struct evbuffer *buf;
	assert((buf = evbuffer_new()));
	assert(!evbuffer_add(buf, (const void *)EX(frame->data), EX(frame->used)));

	if (server->allow_origin[0] != '\0') {
		ADD_HEADER("Access-Control-Allow-Origin", server->allow_origin);
	}
	ADD_HEADER("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate, pre-check=0, post-check=0, max-age=0");
	ADD_HEADER("Pragma", "no-cache");
	ADD_HEADER("Expires", "Mon, 3 Jan 2000 12:34:56 GMT");

	char header_buf[256];

#	define ADD_TIME_HEADER(_key, _value) { \
			snprintf(header_buf, 255, "%.06Lf", _value); \
			ADD_HEADER(_key, header_buf); \
		}

#	define ADD_UNSIGNED_HEADER(_key, _value) { \
			snprintf(header_buf, 255, "%u", _value); \
			ADD_HEADER(_key, header_buf); \
		}

	ADD_TIME_HEADER("X-Timestamp", get_now_real());

	ADD_HEADER("X-UStreamer-Online",						bool_to_string(EX(frame->online)));
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

	server_s *server = (server_s *)v_server;

	PREPROCESS_REQUEST;

	struct evhttp_connection *conn;
	conn = evhttp_request_get_connection(request);

	if (conn) {
		stream_client_s *client;
		A_CALLOC(client, 1);
		client->server = server;
		client->request = request;
		client->need_initial = true;
		client->need_first_frame = true;

		struct evkeyvalq params;
		evhttp_parse_query(evhttp_request_get_uri(request), &params);
#		define PARSE_PARAM(_type, _name) client->_name = uri_get_##_type(&params, #_name)
		PARSE_PARAM(string, key);
		PARSE_PARAM(true, extra_headers);
		PARSE_PARAM(true, advance_headers);
		PARSE_PARAM(true, dual_final_frames);
		PARSE_PARAM(true, zero_data);
#		undef PARSE_PARAM
		evhttp_clear_headers(&params);

		client->hostport = _http_get_client_hostport(request);
		client->id = get_now_id();

		LIST_APPEND_C(RUN(stream_clients), client, RUN(stream_clients_count));

		if (RUN(stream_clients_count) == 1) {
			atomic_store(&VID(has_clients), true);
#			ifdef WITH_GPIO
			gpio_set_has_http_clients(true);
#			endif
		}

		LOG_INFO("HTTP: Registered client: %s, id=%" PRIx64 "; clients now: %u",
			client->hostport, client->id, RUN(stream_clients_count));

		struct bufferevent *buf_event = evhttp_connection_get_bufferevent(conn);
		if (server->tcp_nodelay && !RUN(unix_fd)) {
			evutil_socket_t fd;
			int on = 1;

			LOG_DEBUG("HTTP: Setting up TCP_NODELAY to the client %s ...", client->hostport);
			assert((fd = bufferevent_getfd(buf_event)) >= 0);
			if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) != 0) {
				LOG_PERROR("HTTP: Can't set TCP_NODELAY to the client %s", client->hostport);
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

	stream_client_s *client = (stream_client_s *)v_client;
	server_s *server = client->server;

	long double now = get_now_monotonic();
	long long now_second = floor_ms(now);

	if (now_second != client->fps_accum_second) {
		client->fps = client->fps_accum;
		client->fps_accum = 0;
		client->fps_accum_second = now_second;
	}
	client->fps_accum += 1;

	struct evbuffer *buf;
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
			"Set-Cookie: stream_client=%s/%" PRIx64 "; path=/; max-age=30" RN
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
			(!client->zero_data ? EX(frame->used) : 0),
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
				"X-UStreamer-Latency: %.06Lf" RN
				RN,
				bool_to_string(EX(frame->online)),
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
				now,
				now - EX(frame->grab_ts)
			));
		}
	}

	if (!client->zero_data) {
		assert(!evbuffer_add(buf, (void *)EX(frame->data), EX(frame->used)));
	}
	assert(evbuffer_add_printf(buf, RN "--" BOUNDARY RN));

	if (client->advance_headers) {
		ADD_ADVANCE_HEADERS;
	}

	assert(!bufferevent_write_buffer(buf_event, buf));
	evbuffer_free(buf);

	bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
	bufferevent_enable(buf_event, EV_READ);

#	undef ADD_ADVANCE_HEADERS
#	undef BOUNDARY
}

static void _http_callback_stream_error(UNUSED struct bufferevent *buf_event, UNUSED short what, void *v_client) {
	stream_client_s *client = (stream_client_s *)v_client;
	server_s *server = client->server;

	LIST_REMOVE_C(RUN(stream_clients), client, RUN(stream_clients_count));

	if (RUN(stream_clients_count) == 0) {
		atomic_store(&VID(has_clients), false);
#		ifdef WITH_GPIO
		gpio_set_has_http_clients(false);
#		endif
	}

	char *reason = bufferevent_my_format_reason(what);
	LOG_INFO("HTTP: Disconnected client: %s, id=%" PRIx64 ", %s; clients now: %u",
		client->hostport, client->id, reason, RUN(stream_clients_count));
	free(reason);

	struct evhttp_connection *conn = evhttp_request_get_connection(client->request);
	if (conn) {
		evhttp_connection_free(conn);
	}

	free(client->key);
	free(client->hostport);
	free(client);
}

static void _http_queue_send_stream(server_s *server, bool stream_updated, bool frame_updated) {
	bool has_clients = false;
	bool queued = false;

	LIST_ITERATE(RUN(stream_clients), client, {
		struct evhttp_connection *conn = evhttp_request_get_connection(client->request);
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
				struct bufferevent *buf_event = evhttp_connection_get_bufferevent(conn);
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
	});

	if (queued) {
		static unsigned queued_fps_accum = 0;
		static long long queued_fps_second = 0;
		long long now = floor_ms(get_now_monotonic());
		if (now != queued_fps_second) {
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
	server_s *server = (server_s *)v_server;
	bool stream_updated = false;
	bool frame_updated = false;

	if (atomic_load(&VID(updated))) {
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
			EX(notify_last_online) != EX(frame->online)
			|| EX(notify_last_width) != EX(frame->width)
			|| EX(notify_last_height) != EX(frame->height)
		)
	) {
		EX(notify_last_online) = EX(frame->online);
		EX(notify_last_width) = EX(frame->width);
		EX(notify_last_height) = EX(frame->height);
		process_notify_parent();
	}
}

static bool _expose_new_frame(server_s *server) {
	bool updated = false;

	A_MUTEX_LOCK(&VID(mutex));

	LOG_DEBUG("HTTP: Updating exposed frame (online=%d) ...", VID(frame->online));

	EX(captured_fps) = VID(captured_fps);
	EX(expose_begin_ts) = get_now_monotonic();

	if (server->drop_same_frames && VID(frame->online)) {
		bool need_drop = false;
		bool maybe_same = false;
		if (
			(need_drop = (EX(dropped) < server->drop_same_frames))
			&& (maybe_same = frame_compare(EX(frame), VID(frame)))
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

	frame_copy(VID(frame), EX(frame));

	EX(dropped) = 0;
	EX(expose_cmp_ts) = EX(expose_begin_ts);
	EX(expose_end_ts) = get_now_monotonic();

	LOG_VERBOSE("HTTP: Exposed frame: online=%d, exp_time=%.06Lf",
		 EX(frame->online), EX(expose_end_ts) - EX(expose_begin_ts));

	updated = true;
	not_updated:
		atomic_store(&VID(updated), false);
		A_MUTEX_UNLOCK(&VID(mutex));
		return updated;
}

#undef EX
#undef VID
#undef STREAM
#undef RUN

static char *_http_get_client_hostport(struct evhttp_request *request) {
	char *addr = NULL;
	unsigned short port = 0;
	struct evhttp_connection *conn = evhttp_request_get_connection(request);
	if (conn) {
		char *peer;
		evhttp_connection_get_peer(conn, &peer, &port);
		assert(addr = strdup(peer));
	}

	const char *xff = evhttp_find_header(evhttp_request_get_input_headers(request), "X-Forwarded-For");
	if (xff) {
		if (addr) {
			free(addr);
		}
		assert(addr = strndup(xff, 1024));
		for (unsigned index = 0; addr[index]; ++index) {
			if (addr[index] == ',') {
				addr[index] = '\0';
				break;
			}
		}
	}

	if (addr == NULL) {
		assert(addr = strdup("???"));
	}

	char *hostport;
	A_ASPRINTF(hostport, "[%s]:%u", addr, port);
	free(addr);
	return hostport;
}
