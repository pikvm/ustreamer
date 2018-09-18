#include <assert.h>
#ifdef NDEBUG
#	error WTF dude? Asserts are good things!
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>

#include "tools.h"
#include "device.h"
#include "stream.h"
#include "http.h"


static const char _short_opts[] = "d:f:a:e:tn:q:s:p:h";
static const struct option _long_opts[] = {
	{"device",					required_argument,	NULL,	'd'},
	{"format",					required_argument,	NULL,	'f'},
	{"tv-standard",				required_argument,	NULL,	'a'},
	{"every-frame",				required_argument,	NULL,	'e'},
	{"min-frame-size",			required_argument,	NULL,	'z'},
	{"dv-timings",				no_argument,		NULL,	't'},
	{"buffers",					required_argument,	NULL,	'n'},
	{"jpeg-quality",			required_argument,	NULL,	'q'},
	{"width",					required_argument,	NULL,	1000},
	{"height",					required_argument,	NULL,	1001},
	{"device-timeout",			required_argument,	NULL,	1002},
	{"device-error-timeout",	required_argument,	NULL,	1003},

	{"host",					required_argument,	NULL,	's'},
	{"port",					required_argument,	NULL,	'p'},

	{"debug",					no_argument,		NULL,	5000},
	{"log-level",				required_argument,	NULL,	5001},
	{"help",					no_argument,		NULL,	'h'},
	{NULL, 0, NULL, 0}
};

static void _help() {
	printf("No manual yet\n");
}

static int _parse_options(int argc, char *argv[], struct device_t *dev, struct http_server_t *server) {
#	define OPT_ARG(_dest) \
		{ _dest = optarg; break; }

#	define OPT_TRUE(_dest) \
		{ _dest = true; break; }

#	define OPT_UNSIGNED(_dest, _name, _min) \
		{ int _tmp = strtol(optarg, NULL, 0); \
		if (errno || _tmp < _min) \
		{ printf("Invalid value for '%s=%u'; minimal=%u\n", _name, _tmp, _min); return -1; } \
		_dest = _tmp; break; }

#	define OPT_PARSE(_dest, _func, _invalid, _name) \
		{ if ((_dest = _func(optarg)) == _invalid) \
		{ printf("Unknown " _name ": %s\n", optarg); return -1; } \
		break; }

	int index;
	int ch;

	log_level = LOG_LEVEL_INFO;
	while ((ch = getopt_long(argc, argv, _short_opts, _long_opts, &index)) >= 0) {
		switch (ch) {
			case 'd':	OPT_ARG(dev->path);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case 'f':	OPT_PARSE(dev->format, device_parse_format, FORMAT_UNKNOWN, "pixel format");
#			pragma GCC diagnostic pop
			case 'a':	OPT_PARSE(dev->standard, device_parse_standard, STANDARD_UNKNOWN, "TV standard");
			case 'e':	OPT_UNSIGNED(dev->every_frame, "--every-frame", 1);
			case 'z':	OPT_UNSIGNED(dev->min_frame_size, "--min-frame-size", 0);
			case 't':	OPT_TRUE(dev->dv_timings);
			case 'n':	OPT_UNSIGNED(dev->n_buffers, "--buffers", 1);
			case 'q':	OPT_UNSIGNED(dev->jpeg_quality, "--jpeg-quality", 1);
			case 1000:	OPT_UNSIGNED(dev->width, "--width", 320);
			case 1001:	OPT_UNSIGNED(dev->height, "--height", 180);
			case 1002:	OPT_UNSIGNED(dev->timeout, "--timeout", 1);
			case 1003:	OPT_UNSIGNED(dev->error_timeout, "--error-timeout", 1);

			case 's':	server->host = optarg; break;
			case 'p':	OPT_UNSIGNED(server->port, "--port", 1);

			case 5000:	log_level = LOG_LEVEL_DEBUG; break;
			case 5001:	OPT_UNSIGNED(log_level, "--log-level", 0);
			case 0:		break;
			case 'h':	default: _help(); return -1;
		}
	}

#	undef OPT_PARSE
#	undef OPT_UNSIGNED
#	undef OPT_TRUE
#	undef OPT_ARG

	return 0;
}

struct main_context_t {
	struct device_t			*dev;
	struct stream_t			*stream;
	struct http_server_t	*server;
};

static struct main_context_t *_ctx;

static void _block_thread_signals() {
	sigset_t mask;
	assert(!sigemptyset(&mask));
	assert(!sigaddset(&mask, SIGINT));
	assert(!sigaddset(&mask, SIGTERM));
	assert(!pthread_sigmask(SIG_BLOCK, &mask, NULL));
}

static void *_stream_loop_thread(UNUSED void *_) {
	_block_thread_signals();
	stream_loop(_ctx->dev, _ctx->stream);
	return NULL;
}

static void *_server_loop_thread(UNUSED void *_) {
	_block_thread_signals();
	http_server_loop(_ctx->server);
	return NULL;
}

static void _signal_handler(int signum) {
	LOG_INFO("===== Stopping by %s =====", strsignal(signum));
	stream_loop_break(_ctx->dev);
	http_server_loop_break(_ctx->server);
}

static void _install_signal_handlers() {
	struct sigaction sig_act;

	MEMSET_ZERO(sig_act);
	assert(!sigemptyset(&sig_act.sa_mask));
	sig_act.sa_handler = _signal_handler;
	assert(!sigaddset(&sig_act.sa_mask, SIGINT));
	assert(!sigaddset(&sig_act.sa_mask, SIGTERM));

	LOG_INFO("Installing SIGINT handler ...");
	assert(!sigaction(SIGINT, &sig_act, NULL));

	LOG_INFO("Installing SIGTERM handler ...");
	assert(!sigaction(SIGTERM, &sig_act, NULL));

	LOG_INFO("Ignoring SIGPIPE ...");
	assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
}

int main(int argc, char *argv[]) {
	struct device_t *dev;
	struct stream_t *stream;
	struct http_server_t *server;
	int exit_code = 0;

	dev = device_init();
	stream = stream_init();
	server = http_server_init(stream);

	if ((exit_code = _parse_options(argc, argv, dev, server)) == 0) {
		_install_signal_handlers();

		pthread_t stream_loop_tid;
		pthread_t server_loop_tid;
		struct main_context_t ctx;

		ctx.dev = dev;
		ctx.stream = stream;
		ctx.server = server;
		_ctx = &ctx;

		if ((exit_code = http_server_listen(server)) == 0) {
			A_PTHREAD_CREATE(&stream_loop_tid, _stream_loop_thread, NULL);
			A_PTHREAD_CREATE(&server_loop_tid, _server_loop_thread, NULL);
			A_PTHREAD_JOIN(stream_loop_tid);
			A_PTHREAD_JOIN(server_loop_tid);
		}
	}

	http_server_destroy(server);
	stream_destroy(stream);
	device_destroy(dev);
	return abs(exit_code);
}
