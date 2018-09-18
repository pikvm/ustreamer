#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <event2/event.h>
#include <event2/event-config.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/buffer.h>

#ifndef EVTHREAD_USE_PTHREADS_IMPLEMENTED
#	error Required libevent-pthreads support
#endif

#include "tools.h"
#include "stream.h"
#include "http.h"


static const char DEFAULT_HOST[] = "localhost";


static void _http_callback_root(struct evhttp_request *request, void *arg);
static void _http_callback_stream_ping(struct evhttp_request *request, void *v_server);
static void _http_callback_stream_snapshot(struct evhttp_request *request, void *v_server);
static void _http_update_exposed(struct http_server_t *server);
static void _http_add_header(struct evhttp_request *request, const char *key, const char *value);


struct http_server_t *http_server_init(struct stream_t *stream) {
	struct http_server_runtime_t *run;
	struct http_server_t *server;
	struct exposed_t *exposed;

	A_CALLOC(exposed, 1);

	A_CALLOC(run, 1);
	run->stream = stream;
	run->exposed = exposed;

	A_CALLOC(server, 1);
	server->host = (char *)DEFAULT_HOST;
	server->port = 8080;
	server->run = run;

	assert(!evthread_use_pthreads());
	assert((run->base = event_base_new()));
	assert((run->http = evhttp_new(run->base)));
	evhttp_set_allowed_methods(run->http, EVHTTP_REQ_GET); // TODO: HEAD

	assert(!evhttp_set_cb(run->http, "/", _http_callback_root, NULL));
	assert(!evhttp_set_cb(run->http, "/ping", _http_callback_stream_ping, (void *)server));
	assert(!evhttp_set_cb(run->http, "/snapshot", _http_callback_stream_snapshot, (void *)server));
	return server;
}

void http_server_destroy(struct http_server_t *server) {
	evhttp_free(server->run->http);
	event_base_free(server->run->base);
	free(server->run->exposed->picture.data);
	free(server->run->exposed);
	free(server->run);
	free(server);
	libevent_global_shutdown();
}

int http_server_listen(struct http_server_t *server) {
	LOG_DEBUG("Binding HTTP to [%s]:%d ...", server->host, server->port);
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
	LOG_INFO("Stopping HTTP eventloop ...");
	event_base_loopbreak(server->run->base);
}


static void _http_callback_root(struct evhttp_request *request, UNUSED void *arg) {
	struct evbuffer *buf;

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf,
		"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
		"<title>uStreamer</title></head><body><ul>"
		"<li><a href=\"/ping\">/ping</a></li>"
		"<li><a href=\"/snapshot\">/snapshot</a></li>"
		"</body></html>"
	));
	_http_add_header(request, "Content-Type", "text/html");
	evhttp_send_reply(request, 200, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_stream_ping(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;

	_http_update_exposed(server);

	assert((buf = evbuffer_new()));
	assert(evbuffer_add_printf(buf,
		"{\"stream\": {\"resolution\":"
		" {\"width\": %u, \"height\": %u},"
		" \"online\": %s}}",
		server->run->exposed->width,
		server->run->exposed->height,
		(server->run->exposed->online ? "true" : "false")
	));
	_http_add_header(request, "Content-Type", "application/json");
	evhttp_send_reply(request, 200, "OK", buf);
	evbuffer_free(buf);
}

static void _http_callback_stream_snapshot(struct evhttp_request *request, void *v_server) {
	struct http_server_t *server = (struct http_server_t *)v_server;
	struct evbuffer *buf;
	struct timespec now_spec;
	char now_str[64];

	_http_update_exposed(server);

	assert((buf = evbuffer_new()));
	assert(!evbuffer_add(buf, (const void *)server->run->exposed->picture.data, server->run->exposed->picture.size));

	assert(!clock_gettime(CLOCK_REALTIME, &now_spec));
	sprintf(now_str, "%u.%06u", (unsigned)now_spec.tv_sec, (unsigned)(now_spec.tv_nsec / 1000)); // TODO: round?

	_http_add_header(request, "Access-Control-Allow-Origin:", "*"); // TODO: need this?
	_http_add_header(request, "Cache-Control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
	_http_add_header(request, "Pragma", "no-cache");
	_http_add_header(request, "Expires", "Mon, 3 Jan 2000 12:34:56 GMT");
	_http_add_header(request, "X-Timestamp", now_str);
	_http_add_header(request, "Content-Type", "image/jpeg");

	evhttp_send_reply(request, 200, "OK", buf);
	evbuffer_free(buf);
}

static void _http_update_exposed(struct http_server_t *server) {
	if (server->run->stream->updated) {
		A_PTHREAD_M_LOCK(&server->run->stream->mutex);
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
		A_PTHREAD_M_UNLOCK(&server->run->stream->mutex);
	}
}

static void _http_add_header(struct evhttp_request *request, const char *key, const char *value) {
	assert(!evhttp_add_header(evhttp_request_get_output_headers(request), key, value));
}
