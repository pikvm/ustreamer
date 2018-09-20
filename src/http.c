#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#ifndef EVTHREAD_USE_PTHREADS_IMPLEMENTED
#	error Required libevent-pthreads support
#endif

#include "tools.h"
#include "logging.h"
#include "stream.h"
#include "http.h"


#define BOUNDARY "boundarydonotcross"
#define RN "\r\n"


static void _http_callback_root(struct evhttp_request *request, void *arg);
static void _http_callback_ping(struct evhttp_request *request, void *v_server);
static void _http_callback_snapshot(struct evhttp_request *request, void *v_server);

static void _http_callback_stream(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_ctx);
static void _http_callback_stream_error(struct bufferevent *buf_event, short what, void *v_ctx);

static void _http_send_stream(struct http_server_t *server);
static void _http_exposed_refresh(int fd, short event, void *v_server);


struct http_server_t *http_server_init(struct stream_t *stream) {
	struct http_server_runtime_t *run;
	struct http_server_t *server;
	struct exposed_t *exposed;

	A_CALLOC(exposed, 1);

	A_CALLOC(run, 1);
	run->stream = stream;
	run->exposed = exposed;
	run->refresh_interval.tv_sec = 0;
	run->refresh_interval.tv_usec = 30000; // ~30 refreshes per second

	A_CALLOC(server, 1);
	server->host = "localhost";
	server->port = 8080;
	server->timeout = 10;
	server->run = run;

	assert(!evthread_use_pthreads());
	assert((run->base = event_base_new()));
	assert((run->http = evhttp_new(run->base)));
	evhttp_set_allowed_methods(run->http, EVHTTP_REQ_GET); // TODO: HEAD

	assert(!evhttp_set_cb(run->http, "/", _http_callback_root, NULL));
	assert(!evhttp_set_cb(run->http, "/ping", _http_callback_ping, (void *)exposed));
	assert(!evhttp_set_cb(run->http, "/snapshot", _http_callback_snapshot, (void *)exposed));
	assert(!evhttp_set_cb(run->http, "/stream", _http_callback_stream, (void *)server));

	assert((run->refresh = event_new(run->base, -1, EV_PERSIST, _http_exposed_refresh, server)));
	assert(!event_add(run->refresh, &run->refresh_interval));
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
	LOG_DEBUG("Binding HTTP to [%s]:%d ...", server->host, server->port);
	evhttp_set_timeout(server->run->http, server->timeout);
	if (evhttp_bind_socket(server->run->http, server->host, server->port) != 0) {
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


INLINE static void _http_add_header(struct evhttp_request *request, const char *key, const char *value) {
	assert(!evhttp_add_header(evhttp_request_get_output_headers(request), key, value));
}

static void _http_callback_root(struct evhttp_request *request, UNUSED void *arg) {
	struct evbuffer *buf;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf,
		"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
		"<title>uStreamer</title></head><body><ul>"
		"<li><a href=\"/ping\">/ping</a></li>"
		"<li><a href=\"/snapshot\">/snapshot</a></li>"
		"<li><a href=\"/stream\">/stream</a></li>"
		"</body></html>"
	));
	_http_add_header(request, "Content-Type", "text/html");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_ping(struct evhttp_request *request, void *v_exposed) {
	struct exposed_t *exposed = (struct exposed_t *)v_exposed;
	struct evbuffer *buf;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf,
		"{\"stream\": {\"resolution\":"
		" {\"width\": %u, \"height\": %u},"
		" \"online\": %s}}",
		exposed->width, exposed->height,
		(exposed->online ? "true" : "false")
	));
	_http_add_header(request, "Content-Type", "application/json");
	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_snapshot(struct evhttp_request *request, void *v_exposed) {
	struct exposed_t *exposed = (struct exposed_t *)v_exposed;
	struct evbuffer *buf;
	struct timespec x_timestamp_spec;
	char x_timestamp_buf[64];

	assert((buf = evbuffer_new()));
	assert(!evbuffer_add(buf, (const void *)exposed->picture.data, exposed->picture.size));

	assert(!clock_gettime(CLOCK_REALTIME, &x_timestamp_spec));
	sprintf(
		x_timestamp_buf, "%u.%06u",
		(unsigned)x_timestamp_spec.tv_sec,
		(unsigned)(x_timestamp_spec.tv_nsec / 1000) // TODO: round?
	);

	_http_add_header(request, "Access-Control-Allow-Origin:", "*");
	_http_add_header(request, "Cache-Control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
	_http_add_header(request, "Pragma", "no-cache");
	_http_add_header(request, "Expires", "Mon, 3 Jan 2000 12:34:56 GMT");
	_http_add_header(request, "X-Timestamp", x_timestamp_buf);
	_http_add_header(request, "Content-Type", "image/jpeg");

	evhttp_send_reply(request, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

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

	conn = evhttp_request_get_connection(request);
	if (conn != NULL) {
		A_CALLOC(client, 1);
		client->server = server;
		client->request = request;
		client->need_initial = true;

		if (server->run->stream_clients == NULL) {
			server->run->stream_clients = client;
		} else {
			struct stream_client_t *last = server->run->stream_clients;

			for (; last->next != NULL; last = last->next);
			client->prev = last;
			last->next = client;
		}

		buf_event = evhttp_connection_get_bufferevent(conn);
		bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
		bufferevent_enable(buf_event, EV_READ);
	} else {
		evhttp_request_free(request);
	}
}

static void _http_callback_stream_write(struct bufferevent *buf_event, void *v_client) {
	struct stream_client_t *client = (struct stream_client_t *)v_client;
	struct evbuffer *buf;
	struct timespec x_timestamp_spec;

	assert((buf = evbuffer_new()));
	assert(!clock_gettime(CLOCK_REALTIME, &x_timestamp_spec));

	if (client->need_initial) {
		assert(evbuffer_add_printf(buf,
			"HTTP/1.0 200 OK" RN
			"Access-Control-Allow-Origin: *" RN
			"Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0" RN
			"Pragma: no-cache" RN
			"Expires: Mon, 3 Jan 2000 12:34:56 GMT" RN
			"Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY RN
			RN
			"--" BOUNDARY RN
		));
		assert(!bufferevent_write_buffer(buf_event, buf)); // FIXME
		client->need_initial = false;
	}

	assert(evbuffer_add_printf(buf,
		"Content-Type: image/jpeg" RN
		"Content-Length: %lu" RN
		"X-Timestamp: %u.%06u" RN
		RN,
		client->server->run->exposed->picture.size * sizeof(*client->server->run->exposed->picture.data),
		(unsigned)x_timestamp_spec.tv_sec,
		(unsigned)(x_timestamp_spec.tv_nsec / 1000) // TODO: round?
	));
	assert(!evbuffer_add(buf,
		(void *)client->server->run->exposed->picture.data,
		client->server->run->exposed->picture.size * sizeof(*client->server->run->exposed->picture.data)
	));
	assert(evbuffer_add_printf(buf, RN "--" BOUNDARY RN));

	assert(!bufferevent_write_buffer(buf_event, buf)); // FIXME
	evbuffer_free(buf);

	bufferevent_setcb(buf_event, NULL, NULL, _http_callback_stream_error, (void *)client);
	bufferevent_enable(buf_event, EV_READ);
}

static void _http_callback_stream_error(UNUSED struct bufferevent *buf_event, UNUSED short what, void *v_client) {
	struct stream_client_t *client = (struct stream_client_t *)v_client;
	struct evhttp_connection *conn;

	conn = evhttp_request_get_connection(client->request);
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

static void _http_send_stream(struct http_server_t *server) {
	struct stream_client_t *client;
	struct evhttp_connection *conn;
	struct bufferevent *buf_event;

	for (client = server->run->stream_clients; client != NULL; client = client->next) {
		conn = evhttp_request_get_connection(client->request);
		if (conn != NULL) {
			buf_event = evhttp_connection_get_bufferevent(conn);
			bufferevent_setcb(buf_event, NULL, _http_callback_stream_write, _http_callback_stream_error, (void *)client);
			bufferevent_enable(buf_event, EV_READ|EV_WRITE);
		}
	}
}

static void _http_exposed_refresh(UNUSED int fd, UNUSED short what, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;

	if (server->run->stream->updated) {
		LOG_DEBUG("Refreshing HTTP exposed ...");

		A_PTHREAD_M_LOCK(&server->run->stream->mutex);
		if (server->run->stream->picture.size > 0) {
			if (server->run->stream->picture.allocated > server->run->exposed->picture.allocated) {
				A_REALLOC(server->run->exposed->picture.data, server->run->stream->picture.allocated);
				server->run->exposed->picture.allocated = server->run->stream->picture.allocated;
			}
			memcpy(
				server->run->exposed->picture.data,
				server->run->stream->picture.data,
				server->run->stream->picture.size * sizeof(*server->run->exposed->picture.data)
			);
			server->run->exposed->picture.size = server->run->stream->picture.size;
			server->run->exposed->width = server->run->stream->width;
			server->run->exposed->height = server->run->stream->height;
			server->run->exposed->online = server->run->stream->online;
			server->run->stream->updated = false;
		}
		A_PTHREAD_M_UNLOCK(&server->run->stream->mutex);

		_http_send_stream(server);
	}
}
