#include <signal.h>

#include "tools.h"
#include "capture.h"


struct http_server_t {
	char		*host;
	unsigned	port;
};


struct http_server_t *http_server_init();
void http_server_destroy(struct http_server_t *server);

void http_server_loop(struct http_server_t *server, struct captured_picture_t *captured, sig_atomic_t *volatile global_stop);
