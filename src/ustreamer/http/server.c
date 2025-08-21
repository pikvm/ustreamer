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


#include "server.h"

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

#include "../../libs/types.h"
#include "../../libs/tools.h"
#include "../../libs/threading.h"
#include "../../libs/logging.h"
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

#include "tools.h"
#include "mime.h"
#include "static.h"
#ifdef WITH_SYSTEMD
#	include "systemd/systemd.h"
#endif


static int _http_preprocess_request(struct evhttp_request *request, us_server_s *server);

static int _http_check_run_compat_action(struct evhttp_request *request, void *v_server);

static void _http_callback_root(struct evhttp_request *request, void *v_server);
static void _http_callback_favicon(struct evhttp_request *request, void *v_server);
static void _http_callback_static(struct evhttp_request *request, void *v_server);
static void _http_callback_state(struct evhttp_request *request, void *v_server);
static void _http_callback_snapshot(struct evhttp_request *request, void *v_server);

static void _http_callback_stream(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_ctx);
static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_ctx);

static void _http_refresher(int fd, short event, void *v_server);
static void _http_send_stream(us_server_s *server, bool stream_updated, bool frame_updated);
static void _http_send_snapshot(us_server_s *server);

static bool _expose_frame(us_server_s *server, const us_frame_s *frame);


#define _LOG_ERROR(x_msg, ...)	US_LOG_ERROR("HTTP: " x_msg, ##__VA_ARGS__)
#define _LOG_PERROR(x_msg, ...)	US_LOG_PERROR("HTTP: " x_msg, ##__VA_ARGS__)
#define _LOG_INFO(x_msg, ...)		US_LOG_INFO("HTTP: " x_msg, ##__VA_ARGS__)
#define _LOG_VERBOSE(x_msg, ...)	US_LOG_VERBOSE("HTTP: " x_msg, ##__VA_ARGS__)
#define _LOG_DEBUG(x_msg, ...)	US_LOG_DEBUG("HTTP: " x_msg, ##__VA_ARGS__)

#define _A_EVBUFFER_NEW(x_buf)						assert((x_buf = evbuffer_new()) != NULL)
#define _A_EVBUFFER_ADD(x_buf, x_data, x_size)		assert(!evbuffer_add(x_buf, x_data, x_size))
#define _A_EVBUFFER_ADD_PRINTF(x_buf, x_fmt, ...)	assert(evbuffer_add_printf(x_buf, x_fmt, ##__VA_ARGS__) >= 0)

#define _A_ADD_HEADER(x_request, x_key, x_value) \
		assert(!evhttp_add_header(evhttp_request_get_output_headers(x_request), x_key, x_value))


us_server_s *us_server_init(us_stream_s *stream) {
	us_server_exposed_s *exposed;
	US_CALLOC(exposed, 1);
	exposed->frame = us_frame_init();
	exposed->queued_fpsi = us_fpsi_init("MJPEG-QUEUED", false);

	us_server_runtime_s *run;
	US_CALLOC(run, 1);
	run->ext_fd = -1;
	run->exposed = exposed;

	us_server_s *server;
	US_CALLOC(server, 1);
	server->host = "127.0.0.1";
	server->port = 8080;
	server->unix_path = "";
	server->user = "";
	server->passwd = "";
	server->static_path = "";
	server->allow_origin = "";
	server->instance_id = "";
	server->timeout = 10;
	server->stream = stream;
	server->run = run;

	assert(!evthread_use_pthreads());
	assert((run->base = event_base_new()) != NULL);
	assert((run->http = evhttp_new(run->base)) != NULL);
	evhttp_set_allowed_methods(run->http, EVHTTP_REQ_GET|EVHTTP_REQ_HEAD|EVHTTP_REQ_OPTIONS);
	return server;
}

void us_server_destroy(us_server_s *server) {
	us_server_runtime_s *const run = server->run;

	if (run->refresher != NULL) {
		event_del(run->refresher);
		event_free(run->refresher);
	}

	evhttp_free(run->http);
	US_CLOSE_FD(run->ext_fd);
	event_base_free(run->base);

#	if LIBEVENT_VERSION_NUMBER >= 0x02010100
	libevent_global_shutdown();
#	endif

	US_LIST_ITERATE(run->snapshot_clients, client, { // cppcheck-suppress constStatement
		free(client);
	});

	US_LIST_ITERATE(run->stream_clients, client, { // cppcheck-suppress constStatement
		us_fpsi_destroy(client->fpsi);
		free(client->key);
		free(client->hostport);
		free(client);
	});

	US_DELETE(run->auth_token, free);

	us_fpsi_destroy(run->exposed->queued_fpsi);
	us_frame_destroy(run->exposed->frame);
	free(run->exposed);
	free(server->run);
	free(server);
}

int us_server_listen(us_server_s *server) {
	us_server_runtime_s *const run = server->run;
	us_server_exposed_s *const ex = run->exposed;
	us_stream_s *const stream = server->stream;

	{
		if (server->static_path[0] != '\0') {
			_LOG_INFO("Enabling the file server: %s", server->static_path);
			evhttp_set_gencb(run->http, _http_callback_static, (void*)server);
		} else {
			assert(!evhttp_set_cb(run->http, "/", _http_callback_root, (void*)server));
			assert(!evhttp_set_cb(run->http, "/favicon.ico", _http_callback_favicon, (void*)server));
		}
		assert(!evhttp_set_cb(run->http, "/state", _http_callback_state, (void*)server));
		assert(!evhttp_set_cb(run->http, "/snapshot", _http_callback_snapshot, (void*)server));
		assert(!evhttp_set_cb(run->http, "/stream", _http_callback_stream, (void*)server));
	}

	us_frame_copy(stream->run->blank->jpeg, ex->frame);

	{
		struct timeval interval = {0};
		if (stream->cap->desired_fps > 0) {
			interval.tv_usec = 1000000 / (stream->cap->desired_fps * 2);
		} else {
			interval.tv_usec = 16000; // ~60fps
		}
		assert((run->refresher = event_new(run->base, -1, EV_PERSIST, _http_refresher, server)) != NULL);
		assert(!event_add(run->refresher, &interval));
	}

	evhttp_set_timeout(run->http, server->timeout);

	if (server->user[0] != '\0') {
		char *encoded_token = NULL;

		char *raw_token;
		US_ASPRINTF(raw_token, "%s:%s", server->user, server->passwd);
		us_base64_encode((u8*)raw_token, strlen(raw_token), &encoded_token, NULL);
		free(raw_token);

		US_ASPRINTF(run->auth_token, "Basic %s", encoded_token);
		free(encoded_token);

		_LOG_INFO("Using HTTP basic auth");
	}

	if (server->unix_path[0] != '\0') {
		_LOG_DEBUG("Binding server to UNIX socket '%s' ...", server->unix_path);
		if ((run->ext_fd = us_evhttp_bind_unix(
			run->http,
			server->unix_path,
			server->unix_rm,
			server->unix_mode)) < 0
		) {
			return -1;
		}
		_LOG_INFO("Listening HTTP on UNIX socket '%s'", server->unix_path);

#	ifdef WITH_SYSTEMD
	} else if (server->systemd) {
		_LOG_DEBUG("Binding HTTP to systemd socket ...");
		if ((run->ext_fd = us_evhttp_bind_systemd(run->http)) < 0) {
			return -1;
		}
		_LOG_INFO("Listening systemd socket ...");
#	endif

	} else {
		_LOG_DEBUG("Binding HTTP to [%s]:%u ...", server->host, server->port);
		if (evhttp_bind_socket(run->http, server->host, server->port) < 0) {
			_LOG_PERROR("Can't bind HTTP on [%s]:%u", server->host, server->port)
			return -1;
		}
		_LOG_INFO("Listening HTTP on [%s]:%u", server->host, server->port);
	}

	return 0;
}

void us_server_loop(us_server_s *server) {
	_LOG_INFO("Starting eventloop ...");
	event_base_dispatch(server->run->base);
	_LOG_INFO("Eventloop stopped");
}

void us_server_loop_break(us_server_s *server) {
	event_base_loopbreak(server->run->base);
}

static int _http_preprocess_request(struct evhttp_request *request, us_server_s *server) {
	const us_server_runtime_s *const run = server->run;

	atomic_store(&server->stream->run->http->last_request_ts, us_get_now_monotonic());

	if (server->allow_origin[0] != '\0') {
		const char *const cors_headers = us_evhttp_get_header(request, "Access-Control-Request-Headers");
		const char *const cors_method = us_evhttp_get_header(request, "Access-Control-Request-Method");

		_A_ADD_HEADER(request, "Access-Control-Allow-Origin", server->allow_origin);
		_A_ADD_HEADER(request, "Access-Control-Allow-Credentials", "true");
		if (cors_headers != NULL) {
			_A_ADD_HEADER(request, "Access-Control-Allow-Headers", cors_headers);
		}
		if (cors_method != NULL) {
			_A_ADD_HEADER(request, "Access-Control-Allow-Methods", cors_method);
		}
	}

	if (evhttp_request_get_command(request) == EVHTTP_REQ_OPTIONS) {
		evhttp_send_reply(request, HTTP_OK, "OK", NULL);
		return -1;
	}

	if (run->auth_token != NULL) {
		const char *const token = us_evhttp_get_header(request, "Authorization");
		if (token == NULL || strcmp(token, run->auth_token) != 0) {
			_A_ADD_HEADER(request, "WWW-Authenticate", "Basic realm=\"Restricted area\"");
			evhttp_send_reply(request, 401, "Unauthorized", NULL);
			return -1;
		}
	}

	if (evhttp_request_get_command(request) == EVHTTP_REQ_HEAD) {
		evhttp_send_reply(request, HTTP_OK, "OK", NULL);
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

	int retval = -1;

	struct evkeyvalq params;
	evhttp_parse_query(evhttp_request_get_uri(request), &params);
	const char *const action = evhttp_find_header(&params, "action");

	if (action && !strcmp(action, "snapshot")) {
		_http_callback_snapshot(request, v_server);
		retval = 0;
	} else if (action && !strcmp(action, "stream")) {
		_http_callback_stream(request, v_server);
		retval = 0;
	}

	evhttp_clear_headers(&params);
	return retval;
}

#define COMPAT_REQUEST { \
		if (_http_check_run_compat_action(request, v_server) == 0) { \
			return; \
		} \
	}

static void _http_callback_root(struct evhttp_request *request, void *v_server) {
	us_server_s *const server = v_server;

	PREPROCESS_REQUEST;
	COMPAT_REQUEST;

	struct evbuffer *buf;
	_A_EVBUFFER_NEW(buf);
	_A_EVBUFFER_ADD_PRINTF(buf, "%s", US_HTML_INDEX_PAGE);
	_A_ADD_HEADER(request, "Content-Type", "text/html");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);

	evbuffer_free(buf);
}

static void _http_callback_favicon(struct evhttp_request *request, void *v_server) {
	us_server_s *const server = v_server;

	PREPROCESS_REQUEST;

	struct evbuffer *buf;
	_A_EVBUFFER_NEW(buf);
	_A_EVBUFFER_ADD(buf, (const void*)US_FAVICON_ICO_DATA, US_FAVICON_ICO_DATA_SIZE);
	_A_ADD_HEADER(request, "Content-Type", "image/x-icon");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);

	evbuffer_free(buf);
}

static void _http_callback_static(struct evhttp_request *request, void *v_server) {
	us_server_s *const server = v_server;

	PREPROCESS_REQUEST;
	COMPAT_REQUEST;

	struct evbuffer *buf = NULL;
	struct evhttp_uri *uri = NULL;
	char *decoded_path = NULL;
	char *static_path = NULL;
	int fd = -1;

	{
		const char *uri_path;
		if ((uri = evhttp_uri_parse(evhttp_request_get_uri(request))) == NULL) {
			goto bad_request;
		}
		if ((uri_path = (char*)evhttp_uri_get_path(uri)) == NULL) {
			uri_path = "/";
		}
		if ((decoded_path = evhttp_uridecode(uri_path, 0, NULL)) == NULL) {
			goto bad_request;
		}
	}

	_A_EVBUFFER_NEW(buf);

	if ((static_path = us_find_static_file_path(server->static_path, decoded_path)) == NULL) {
		goto not_found;
	}

	if ((fd = open(static_path, O_RDONLY)) < 0) {
		_LOG_PERROR("Can't open found static file %s", static_path);
		goto not_found;
	}

	{
		struct stat st;
		if (fstat(fd, &st) < 0) {
			_LOG_PERROR("Can't stat() found static file %s", static_path);
			goto not_found;
		}
		if (st.st_size > 0 && evbuffer_add_file(buf, fd, 0, st.st_size) < 0) {
			_LOG_ERROR("Can't serve static file %s", static_path);
			goto not_found;
		}

		// evbuffer_add_file() owns the resulting file descriptor
		// and will close it when finished transferring data
		fd = -1;

		_A_ADD_HEADER(request, "Content-Type", us_guess_mime_type(static_path));
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
	US_CLOSE_FD(fd); // cppcheck-suppress unreadVariable
	US_DELETE(static_path, free);
	US_DELETE(buf, evbuffer_free);
	US_DELETE(decoded_path, free);
	US_DELETE(uri, evhttp_uri_free);
}

#undef COMPAT_REQUEST

static void _http_callback_state(struct evhttp_request *request, void *v_server) {
	us_server_s *const server = v_server;
	us_server_runtime_s *const run = server->run;
	us_server_exposed_s *const ex = run->exposed;
	us_stream_s *const stream = server->stream;

	PREPROCESS_REQUEST;

	us_encoder_type_e enc_type;
	uint enc_quality;
	us_encoder_get_runtime_params(stream->enc, &enc_type, &enc_quality);

	struct evbuffer *buf;
	_A_EVBUFFER_NEW(buf);

	_A_EVBUFFER_ADD_PRINTF(buf,
		"{\"ok\": true, \"result\": {"
		" \"instance_id\": \"%s\","
		" \"encoder\": {\"type\": \"%s\", \"quality\": %u},",
		server->instance_id,
		us_encoder_type_to_string(enc_type),
		enc_quality
	);

#	ifdef WITH_V4P
	if (stream->drm != NULL) {
		us_fpsi_meta_s meta;
		const uint fps = us_fpsi_get(stream->run->http->drm_fpsi, &meta);
		_A_EVBUFFER_ADD_PRINTF(buf,
			" \"drm\": {\"live\": %s, \"fps\": %u},",
			us_bool_to_string(meta.online),
			fps
		);
	}
#	endif

	if (stream->h264_sink != NULL) {
		us_fpsi_meta_s meta;
		const uint fps = us_fpsi_get(stream->run->http->h264_fpsi, &meta);
		_A_EVBUFFER_ADD_PRINTF(buf,
			" \"h264\": {\"bitrate\": %u, \"gop\": %u, \"online\": %s, \"fps\": %u},",
			stream->h264_bitrate,
			stream->h264_gop,
			us_bool_to_string(meta.online),
			fps
		);
	}

	if (stream->jpeg_sink != NULL || stream->h264_sink != NULL) {
		_A_EVBUFFER_ADD_PRINTF(buf, " \"sinks\": {");
		if (stream->jpeg_sink != NULL) {
			_A_EVBUFFER_ADD_PRINTF(buf,
				"\"jpeg\": {\"has_clients\": %s}",
				us_bool_to_string(atomic_load(&stream->jpeg_sink->has_clients))
			);
		}
		if (stream->h264_sink != NULL) {
			_A_EVBUFFER_ADD_PRINTF(buf,
				"%s\"h264\": {\"has_clients\": %s}",
				(stream->jpeg_sink ? ", " : ""),
				us_bool_to_string(atomic_load(&stream->h264_sink->has_clients))
			);
		}
		_A_EVBUFFER_ADD_PRINTF(buf, "},");
	}

	us_fpsi_meta_s captured_meta;
	const uint captured_fps = us_fpsi_get(stream->run->http->captured_fpsi, &captured_meta);
	_A_EVBUFFER_ADD_PRINTF(buf,
		" \"source\": {\"resolution\": {\"width\": %u, \"height\": %u},"
		" \"online\": %s, \"desired_fps\": %u, \"captured_fps\": %u},"
		" \"stream\": {\"queued_fps\": %u, \"clients\": %u, \"clients_stat\": {",
		(server->fake_width ? server->fake_width : captured_meta.width),
		(server->fake_height ? server->fake_height : captured_meta.height),
		us_bool_to_string(captured_meta.online),
		stream->cap->desired_fps,
		captured_fps,
		us_fpsi_get(ex->queued_fpsi, NULL),
		run->stream_clients_count
	);

	US_LIST_ITERATE(run->stream_clients, client, { // cppcheck-suppress constStatement
		_A_EVBUFFER_ADD_PRINTF(buf,
			"\"%" PRIx64 "\": {\"fps\": %u, \"extra_headers\": %s, \"advance_headers\": %s,"
			" \"dual_final_frames\": %s, \"zero_data\": %s, \"key\": \"%s\"}%s",
			client->id,
			us_fpsi_get(client->fpsi, NULL),
			us_bool_to_string(client->extra_headers),
			us_bool_to_string(client->advance_headers),
			us_bool_to_string(client->dual_final_frames),
			us_bool_to_string(client->zero_data),
			(client->key != NULL ? client->key : "0"),
			(client->next ? ", " : "")
		);
	});

	_A_EVBUFFER_ADD_PRINTF(buf, "}}}}");

	_A_ADD_HEADER(request, "Content-Type", "application/json");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_snapshot(struct evhttp_request *request, void *v_server) {
	us_server_s *const server = v_server;

	PREPROCESS_REQUEST;

	us_snapshot_client_s *client;
	US_CALLOC(client, 1);
	client->server = server;
	client->request = request;
	client->request_ts = us_get_now_monotonic();

	atomic_fetch_add(&server->stream->run->http->snapshot_requested, 1);
	US_LIST_APPEND(server->run->snapshot_clients, client);
}

static void _http_callback_stream(struct evhttp_request *request, void *v_server) {
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L2814
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L2789
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L362
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L791
	// https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/http.c#L1458

	us_server_s *const server = v_server;
	us_server_runtime_s *const run = server->run;

	PREPROCESS_REQUEST;

	struct evhttp_connection *const conn = evhttp_request_get_connection(request);
	if (conn != NULL) {
		us_stream_client_s *client;
		US_CALLOC(client, 1);
		client->server = server;
		client->request = request;
		client->need_initial = true;
		client->need_first_frame = true;

		struct evkeyvalq params;
		evhttp_parse_query(evhttp_request_get_uri(request), &params);
#		define PARSE_PARAM(x_type, x_name) client->x_name = us_evkeyvalq_get_##x_type(&params, #x_name)
		PARSE_PARAM(string, key);
		PARSE_PARAM(true, extra_headers);
		PARSE_PARAM(true, advance_headers);
		PARSE_PARAM(true, dual_final_frames);
		PARSE_PARAM(true, zero_data);
#		undef PARSE_PARAM
		evhttp_clear_headers(&params);

		client->hostport = us_evhttp_get_hostport(request);
		client->id = us_get_now_id();

		{
			char *name;
			US_ASPRINTF(name, "MJPEG-CLIENT-%" PRIx64, client->id);
			client->fpsi = us_fpsi_init(name, false);
			free(name);
		}

		US_LIST_APPEND_C(run->stream_clients, client, run->stream_clients_count);

		if (run->stream_clients_count == 1) {
			atomic_store(&server->stream->run->http->has_clients, true);
#			ifdef WITH_GPIO
			us_gpio_set_has_http_clients(true);
#			endif
		}

		_LOG_INFO("NEW client (now=%u): %s, id=%" PRIx64,
			run->stream_clients_count, client->hostport, client->id);

		struct bufferevent *const buf_event = evhttp_connection_get_bufferevent(conn);
		if (server->tcp_nodelay && run->ext_fd >= 0) {
			_LOG_DEBUG("Setting up TCP_NODELAY to the client %s ...", client->hostport);
			const evutil_socket_t fd = bufferevent_getfd(buf_event);
			assert(fd >= 0);
			int on = 1;
			if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void*)&on, sizeof(on)) != 0) {
				_LOG_PERROR("Can't set TCP_NODELAY to the client %s", client->hostport);
			}
		}
		bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void*)client);
		bufferevent_enable(buf_event, EV_READ);
	} else {
		evhttp_request_free(request);
	}
}

#undef PREPROCESS_REQUEST

static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_client) {
	us_stream_client_s *const client = v_client;
	us_server_s *const server = client->server;
	us_server_exposed_s *const ex = server->run->exposed;

	us_fpsi_update(client->fpsi, true, NULL);

	struct evbuffer *buf;
	_A_EVBUFFER_NEW(buf);

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
	// Его наличие не требуется RFC, однако никаких стандартов на MJPEG over HTTP
	// в природе не существует, и никто не может гарантировать, что отсутствие
	// Content-Length не сломает вещание для каких-нибудь маргинальных браузеров.
	//
	// Кроме того, advance_headers форсит отключение заголовков X-UStreamer-*
	// по тем же причинам, по которым у нас нет Content-Length.

#	define BOUNDARY "boundarydonotcross"

#	define ADD_ADVANCE_HEADERS \
		_A_EVBUFFER_ADD_PRINTF(buf, \
			"Content-Type: image/jpeg" RN "X-Timestamp: %.06Lf" RN RN, us_get_now_real())

	if (client->need_initial) {
		_A_EVBUFFER_ADD_PRINTF(buf, "HTTP/1.0 200 OK" RN);
		
		if (client->server->allow_origin[0] != '\0') {
			const char *const cors_headers = us_evhttp_get_header(client->request, "Access-Control-Request-Headers");
			const char *const cors_method = us_evhttp_get_header(client->request, "Access-Control-Request-Method");

			_A_EVBUFFER_ADD_PRINTF(buf,
				"Access-Control-Allow-Origin: %s" RN
				"Access-Control-Allow-Credentials: true" RN,
				client->server->allow_origin				
			);
			if (cors_headers != NULL) {
				_A_EVBUFFER_ADD_PRINTF(buf, "Access-Control-Allow-Headers: %s" RN, cors_headers);
			}
			if (cors_method != NULL) {
				_A_EVBUFFER_ADD_PRINTF(buf, "Access-Control-Allow-Methods: %s" RN, cors_method);
			}
		}

		_A_EVBUFFER_ADD_PRINTF(buf,
			"Cache-Control: no-store, no-cache, must-revalidate, proxy-revalidate, pre-check=0, post-check=0, max-age=0" RN
			"Pragma: no-cache" RN
			"Expires: Mon, 3 Jan 2000 12:34:56 GMT" RN
			"Set-Cookie: stream_client%s%s=%s/%" PRIx64 "; path=/; max-age=30" RN
			"Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY RN
			RN
			"--" BOUNDARY RN,
			(server->instance_id[0] == '\0' ? "" : "_"),
			server->instance_id,
			(client->key != NULL ? client->key : "0"),
			client->id
		);

		if (client->advance_headers) {
			ADD_ADVANCE_HEADERS;
		}

		assert(!bufferevent_write_buffer(buf_event, buf));
		client->need_initial = false;
	}

	if (!client->advance_headers) {
		_A_EVBUFFER_ADD_PRINTF(buf,
			"Content-Type: image/jpeg" RN
			"Content-Length: %zu" RN
			"X-Timestamp: %.06Lf" RN
			"%s",
			(!client->zero_data ? ex->frame->used : 0),
			us_get_now_real(),
			(client->extra_headers ? "" : RN)
		);
		const ldf now_ts = us_get_now_monotonic();
		if (client->extra_headers) {
			_A_EVBUFFER_ADD_PRINTF(buf,
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
				us_bool_to_string(ex->frame->online),
				ex->dropped,
				ex->frame->width,
				ex->frame->height,
				us_fpsi_get(client->fpsi, NULL),
				ex->frame->grab_ts,
				ex->frame->encode_begin_ts,
				ex->frame->encode_end_ts,
				ex->expose_begin_ts,
				ex->expose_cmp_ts,
				ex->expose_end_ts,
				now_ts,
				now_ts - ex->frame->grab_ts
			);
		}
	}

	if (!client->zero_data) {
		_A_EVBUFFER_ADD(buf, (void*)ex->frame->data, ex->frame->used);
	}
	_A_EVBUFFER_ADD_PRINTF(buf, RN "--" BOUNDARY RN);

	if (client->advance_headers) {
		ADD_ADVANCE_HEADERS;
	}

	assert(!bufferevent_write_buffer(buf_event, buf));
	evbuffer_free(buf);

	bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void*)client);
	bufferevent_enable(buf_event, EV_READ);

#	undef ADD_ADVANCE_HEADERS
#	undef BOUNDARY
}

static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_client) {
	(void)buf_event;
	(void)what;

	us_stream_client_s *const client = v_client;
	us_server_s *const server = client->server;
	us_server_runtime_s *const run = server->run;

	US_LIST_REMOVE_C(run->stream_clients, client, run->stream_clients_count);

	if (run->stream_clients_count == 0) {
		atomic_store(&server->stream->run->http->has_clients, false);
#		ifdef WITH_GPIO
		us_gpio_set_has_http_clients(false);
#		endif
	}

	char *const reason = us_bufferevent_format_reason(what);
	_LOG_INFO("DEL client (now=%u): %s, id=%" PRIx64 ", %s",
		run->stream_clients_count, client->hostport, client->id, reason);
	free(reason);

	struct evhttp_connection *conn = evhttp_request_get_connection(client->request);
	US_DELETE(conn, evhttp_connection_free);

	us_fpsi_destroy(client->fpsi);
	free(client->key);
	free(client->hostport);
	free(client);
}

static void _http_send_stream(us_server_s *server, bool stream_updated, bool frame_updated) {
	us_server_runtime_s *const run = server->run;
	us_server_exposed_s *const ex = run->exposed;

	bool queued = false;
	bool has_clients = true;

	US_LIST_ITERATE(run->stream_clients, client, { // cppcheck-suppress constStatement
		struct evhttp_connection *const conn = evhttp_request_get_connection(client->request);
		if (conn != NULL) {
			// Фикс для бага WebKit. При включенной опции дропа одинаковых фреймов,
			// WebKit отрисовывает последний фрейм в серии с некоторой задержкой,
			// и нужно послать два фрейма, чтобы серия была вовремя завершена.
			// Это похоже на баг Blink (см. _http_callback_stream_write() и advance_headers),
			// но фикс для него не лечит проблему вебкита. Такие дела.

			const bool dual_update = (
				server->drop_same_frames
				&& client->dual_final_frames
				&& stream_updated
				&& client->updated_prev
				&& !frame_updated
			);

			if (dual_update || frame_updated || client->need_first_frame) {
				struct bufferevent *const buf_event = evhttp_connection_get_bufferevent(conn);
				bufferevent_setcb(buf_event, NULL, _http_callback_stream_write, _http_callback_stream_error, (void*)client);
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
		us_fpsi_update(ex->queued_fpsi, true, NULL);
	} else if (!has_clients) {
		us_fpsi_update(ex->queued_fpsi, false, NULL);
	}
}

static void _http_send_snapshot(us_server_s *server) {
	us_server_exposed_s *const ex = server->run->exposed;
	us_blank_s *blank = NULL;

#	define ADD_TIME_HEADER(x_key, x_value) { \
			US_SNPRINTF(header_buf, 255, "%.06Lf", x_value); \
			_A_ADD_HEADER(request, x_key, header_buf); \
		}

#	define ADD_UNSIGNED_HEADER(x_key, x_value) { \
			US_SNPRINTF(header_buf, 255, "%u", x_value); \
			_A_ADD_HEADER(request, x_key, header_buf); \
		}

	us_fpsi_meta_s captured_meta;
	us_fpsi_get(server->stream->run->http->captured_fpsi, &captured_meta);

	US_LIST_ITERATE(server->run->snapshot_clients, client, { // cppcheck-suppress constStatement
		struct evhttp_request *request = client->request;

		const bool has_fresh_snapshot = (atomic_load(&server->stream->run->http->snapshot_requested) == 0);
		const bool timed_out = (client->request_ts + US_MAX((uint)1, server->stream->error_delay * 3) < us_get_now_monotonic());

		if (has_fresh_snapshot || timed_out) {
			us_frame_s *frame = ex->frame;
			if (!captured_meta.online) {
				if (blank == NULL) {
					blank = us_blank_init();
					us_blank_draw(blank, "< NO LIVE VIDEO >", captured_meta.width, captured_meta.height);
				}
				frame = blank->jpeg;
			}

			struct evbuffer *buf;
			_A_EVBUFFER_NEW(buf);
			_A_EVBUFFER_ADD(buf, (const void*)frame->data, frame->used);

			_A_ADD_HEADER(request, "Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate, pre-check=0, post-check=0, max-age=0");
			_A_ADD_HEADER(request, "Pragma", "no-cache");
			_A_ADD_HEADER(request, "Expires", "Mon, 3 Jan 2000 12:34:56 GMT");

			char header_buf[256];

			ADD_TIME_HEADER("X-Timestamp", us_get_now_real());

			_A_ADD_HEADER(request, "X-UStreamer-Online",			us_bool_to_string(frame->online));
			ADD_UNSIGNED_HEADER("X-UStreamer-Width",				frame->width);
			ADD_UNSIGNED_HEADER("X-UStreamer-Height",				frame->height);
			ADD_TIME_HEADER("X-UStreamer-Grab-Timestamp",			frame->grab_ts);
			ADD_TIME_HEADER("X-UStreamer-Encode-Begin-Timestamp",	frame->encode_begin_ts);
			ADD_TIME_HEADER("X-UStreamer-Encode-End-Timestamp",		frame->encode_end_ts);
			ADD_TIME_HEADER("X-UStreamer-Send-Timestamp",			us_get_now_monotonic());

			_A_ADD_HEADER(request, "Content-Type", "image/jpeg");

			evhttp_send_reply(request, HTTP_OK, "OK", buf);
			evbuffer_free(buf);

			US_LIST_REMOVE(server->run->snapshot_clients, client);
			free(client);
		}
	});

#	undef ADD_UNSUGNED_HEADER
#	undef ADD_TIME_HEADER

	US_DELETE(blank, us_blank_destroy);
}

static void _http_refresher(int fd, short what, void *v_server) {
	(void)fd;
	(void)what;

	us_server_s *server = v_server;
	us_server_exposed_s *ex = server->run->exposed;
	us_ring_s *const ring = server->stream->run->http->jpeg_ring;

	bool stream_updated = false;
	bool frame_updated = false;

	const int ri = us_ring_consumer_acquire(ring, 0);
	if (ri >= 0) {
		const us_frame_s *const frame = ring->items[ri];
		frame_updated = _expose_frame(server, frame);
		stream_updated = true;
		us_ring_consumer_release(ring, ri);
	} else if (ex->expose_end_ts + 1 < us_get_now_monotonic()) {
		_LOG_DEBUG("Repeating exposed ...");
		ex->expose_begin_ts = us_get_now_monotonic();
		ex->expose_cmp_ts = ex->expose_begin_ts;
		ex->expose_end_ts = ex->expose_begin_ts;
		frame_updated = true;
		stream_updated = true;
	}

	_http_send_stream(server, stream_updated, frame_updated);
	_http_send_snapshot(server);
}

static bool _expose_frame(us_server_s *server, const us_frame_s *frame) {
	us_server_exposed_s *const ex = server->run->exposed;

	_LOG_DEBUG("Updating exposed frame (online=%d) ...", frame->online);
	ex->expose_begin_ts = us_get_now_monotonic();

	if (server->drop_same_frames && frame->online) {
		bool need_drop = false;
		bool maybe_same = false;
		if (
			(need_drop = (ex->dropped < server->drop_same_frames))
			&& (maybe_same = us_frame_compare(ex->frame, frame))
		) {
			ex->expose_cmp_ts = us_get_now_monotonic();
			ex->expose_end_ts = ex->expose_cmp_ts;
			_LOG_VERBOSE("Dropped same frame number %u; cmp_time=%.06Lf",
				ex->dropped, (ex->expose_cmp_ts - ex->expose_begin_ts));
			ex->dropped += 1;
			return false; // Not updated
		} else {
			ex->expose_cmp_ts = us_get_now_monotonic();
			_LOG_VERBOSE("Passed same frame check (need_drop=%d, maybe_same=%d); cmp_time=%.06Lf",
				need_drop, maybe_same, (ex->expose_cmp_ts - ex->expose_begin_ts));
		}
	}

	if (frame->used == 0) {
		// Фрейм нулевой длины означает, что мы просто должны повторить то,
		// что у нас уже есть, с поправкой на онлайн.
		ex->frame->online = frame->online;
	} else {
		us_frame_copy(frame, ex->frame);
	}

	ex->dropped = 0;
	ex->expose_cmp_ts = ex->expose_begin_ts;
	ex->expose_end_ts = us_get_now_monotonic();

	_LOG_VERBOSE("Exposed frame: online=%d, exp_time=%.06Lf",
		 ex->frame->online, (ex->expose_end_ts - ex->expose_begin_ts));
	return true; // Updated
}
