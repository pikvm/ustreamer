#include "capture.h"
#include "tools.h"
#include "http.h"


static const char DEFAULT_HOST[] = "localhost";


struct http_server_t *http_server_init() {
	struct http_server_t *server;

	A_CALLOC(server, 1, sizeof(*server));
	MEMSET_ZERO_PTR(server);

	server->host = (char *)DEFAULT_HOST;
	server->port = 8080;
	return server;
}

void http_server_destroy(struct http_server_t *server) {
	free(server);
}

void http_server_loop(struct http_server_t *server, struct captured_picture_t *captured) {
	// TODO: implement server here
}

void http_server_loop_break(struct http_server_t *server) {
	// TODO: implement stop here
}

