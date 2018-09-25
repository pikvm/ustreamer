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


#include <assert.h>
#ifdef NDEBUG
#	error WTF dude? Asserts are good things!
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>

#include <pthread.h>

#include "config.h"
#include "tools.h"
#include "logging.h"
#include "device.h"
#include "encoder.h"
#include "stream.h"
#include "http.h"


static const char _short_opts[] = "d:x:y:f:a:e:z:tn:w:q:c:s:p:h";
static const struct option _long_opts[] = {
	{"device",					required_argument,	NULL,	'd'},
	{"width",					required_argument,	NULL,	'x'},
	{"height",					required_argument,	NULL,	'y'},
	{"format",					required_argument,	NULL,	'f'},
	{"tv-standard",				required_argument,	NULL,	'a'},
	{"every-frame",				required_argument,	NULL,	'e'},
	{"min-frame-size",			required_argument,	NULL,	'z'},
	{"dv-timings",				no_argument,		NULL,	't'},
	{"buffers",					required_argument,	NULL,	'b'},
	{"workers",					required_argument,	NULL,	'w'},
	{"jpeg-quality",			required_argument,	NULL,	'q'},
	{"encoder",					required_argument,	NULL,	'c'},
	{"device-timeout",			required_argument,	NULL,	1000},
	{"device-error-timeout",	required_argument,	NULL,	1001},

	{"host",					required_argument,	NULL,	's'},
	{"port",					required_argument,	NULL,	'p'},
	{"fake-width",				required_argument,	NULL,	2000},
	{"fake-height",				required_argument,	NULL,	2001},
	{"server-timeout",			required_argument,	NULL,	2002},

	{"debug",					no_argument,		NULL,	5000},
	{"log-level",				required_argument,	NULL,	5001},
	{"help",					no_argument,		NULL,	'h'},
	{NULL, 0, NULL, 0},
};

static void _help(struct device_t *dev, struct http_server_t *server) {
	printf("\nuStreamer - Lightweight and fast MJPG-HTTP streamer\n");
	printf("===================================================\n\n");
	printf("Version: %s; license: GPLv3\n", VERSION);
	printf("Copyright (C) 2018 Maxim Devaev <mdevaev@gmail.com>\n\n");
	printf("Capturing options:\n");
	printf("------------------\n");
	printf("    -d|--device </dev/path>          -- Path to V4L2 device. Default: %s\n\n", dev->path);
	printf("    -x|--width <N>                   -- Initial image width. Default: %d\n\n", dev->width);
	printf("    -y|--height <N>                  -- Initial image height. Default: %d\n\n", dev->height);
	printf("    -f|--format <fmt>                -- Image format.\n");
	printf("                                        Available: %s; default: YUYV.\n\n", FORMATS_STR);
	printf("    -a|--tv-standard <std>           -- Force TV standard.\n");
	printf("                                        Available: %s; default: disabled.\n\n", STANDARDS_STR);
	printf("    -e|--every-frame <N>             -- Drop all input frames except specified. Default: disabled.\n\n");
	printf("    -z|--min-frame-size <N>          -- Drop frames smaller then this limit.\n");
	printf("                                        Useful if the device produces small-sized garbage frames.\n\n");
	printf("    -t|--dv-timings                  -- Enable DV timings queriyng and events processing.\n");
	printf("                                        Supports automatic resolution changing. Default: disabled.\n\n");
	printf("    -b|--buffers <N>                 -- The number of buffers to receive data from the device.\n");
	printf("                                        Each buffer may processed using an intermediate thread.\n");
	printf("                                        Default: %d (number of CPU cores + 1)\n\n", dev->n_buffers);
	printf("    -w|--workers <N>                 -- The number of compressing threads. Default: %d (== --buffers).\n\n", dev->n_workers);
	printf("    -q|--jpeg-quality <N>            -- Set quality of JPEG encoding from 1 to 100 (best). Default: %d.\n\n", dev->jpeg_quality);
	printf("    --encoder <type>                 -- Use specified encoder. It may affects to workers number.\n");
	printf("                                     -- Available: %s; default: CPU.\n\n", ENCODER_TYPES_STR);
	printf("    --device-timeout <seconds>       -- Timeout for device querying. Default: %d\n\n", dev->timeout);
	printf("    --device-error-timeout <seconds> -- Delay before trying to connect to the device again\n");
	printf("                                        after a timeout. Default: %d\n\n", dev->error_timeout);
	printf("HTTP server options:\n");
	printf("--------------------\n");
	printf("    --host <address>           -- Listen on Hostname or IP. Default: %s\n\n", server->host);
	printf("    --port <N>                 -- Bind to this TCP port. Default: %d\n\n", server->port);
	printf("    --fake-width <N>           -- Override image width for /ping. Default: disabled\n\n");
	printf("    --fake-height <N>          -- Override image height for /ping. Default: disabled.\n\n");
	printf("    --server-timeout <seconds> -- Timeout for client connections. Default: %d\n\n", server->timeout);
	printf("Misc options:\n");
	printf("-------------\n");
	printf("    --debug         -- Enabled debug messages (same as --log-level=3). Default: disabled.\n\n");
	printf("    --log-level <N> -- Verbosity level of messages from 0 (info) to 3 (debug).\n");
	printf("                       Enabling debugging messages can slow down the program.\n");
	printf("                       Available levels: 0=info, 1=performance, 2=verbose, 3=debug.\n");
	printf("                       Default: %d.\n\n", log_level);
	printf("    -h|--help       -- Print this messages and exit.\n\n");
}

static int _parse_options(int argc, char *argv[], struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server) {
#	define OPT_ARG(_dest) \
		{ _dest = optarg; break; }

#	define OPT_TRUE(_dest) \
		{ _dest = true; break; }

#	define OPT_UNSIGNED(_dest, _name, _min, _max) \
		{ errno = 0; int _tmp = strtol(optarg, NULL, 0); \
		if (errno || _tmp < _min || _tmp > _max) \
		{ printf("Invalid value for '%s=%u'; minimal=%u; maximum=%u\n", _name, _tmp, _min, _max); return -1; } \
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
			case 'x':	OPT_UNSIGNED(dev->width, "--width", 320, 1920);
			case 'y':	OPT_UNSIGNED(dev->height, "--height", 180, 1200);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case 'f':	OPT_PARSE(dev->format, device_parse_format, FORMAT_UNKNOWN, "pixel format");
#			pragma GCC diagnostic pop
			case 'a':	OPT_PARSE(dev->standard, device_parse_standard, STANDARD_UNKNOWN, "TV standard");
			case 'e':	OPT_UNSIGNED(dev->every_frame, "--every-frame", 1, 30);
			case 'z':	OPT_UNSIGNED(dev->min_frame_size, "--min-frame-size", 0, 8192);
			case 't':	OPT_TRUE(dev->dv_timings);
			case 'b':	OPT_UNSIGNED(dev->n_buffers, "--buffers", 1, 32);
			case 'w':	OPT_UNSIGNED(dev->n_workers, "--workers", 1, 32);
			case 'q':	OPT_UNSIGNED(dev->jpeg_quality, "--jpeg-quality", 1, 100);
			case 'c':	OPT_PARSE(encoder->type, encoder_parse_type, ENCODER_TYPE_UNKNOWN, "encoder type")
			case 1000:	OPT_UNSIGNED(dev->timeout, "--timeout", 1, 60);
			case 1001:	OPT_UNSIGNED(dev->error_timeout, "--error-timeout", 1, 60);

			case 's':	server->host = optarg; break;
			case 'p':	OPT_UNSIGNED(server->port, "--port", 1, 65535);
			case 2000:	OPT_UNSIGNED(server->fake_width, "--fake-width", 0, 1920);
			case 2001:	OPT_UNSIGNED(server->fake_height, "--fake-height", 0, 1200);
			case 2002:	OPT_UNSIGNED(server->timeout, "--server-timeout", 1, 60);

			case 5000:	log_level = LOG_LEVEL_DEBUG; break;
			case 5001:	OPT_UNSIGNED(log_level, "--log-level", 0, 3);
			case 0:		break;
			case 'h':	default: _help(dev, server); return -1;
		}
	}

#	undef OPT_PARSE
#	undef OPT_UNSIGNED
#	undef OPT_TRUE
#	undef OPT_ARG
	return 0;
}

struct main_context_t {
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

static void *_stream_loop_thread(UNUSED void *arg) {
	_block_thread_signals();
	stream_loop(_ctx->stream);
	return NULL;
}

static void *_server_loop_thread(UNUSED void *arg) {
	_block_thread_signals();
	http_server_loop(_ctx->server);
	return NULL;
}

static void _signal_handler(int signum) {
	LOG_INFO_NOLOCK("===== Stopping by %s =====", (signum == SIGTERM ? "SIGTERM" : "SIGINT"));
	stream_loop_break(_ctx->stream);
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
	struct encoder_t *encoder;
	struct stream_t *stream;
	struct http_server_t *server;
	int exit_code = 0;

	LOGGING_INIT;

	dev = device_init();
	encoder = encoder_init(ENCODER_TYPE_CPU);
	stream = stream_init(dev, encoder);
	server = http_server_init(stream);

	if ((exit_code = _parse_options(argc, argv, dev, encoder, server)) == 0) {
		_install_signal_handlers();
		encoder_prepare(encoder);

		pthread_t stream_loop_tid;
		pthread_t server_loop_tid;
		struct main_context_t ctx;

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
	encoder_destroy(encoder);
	device_destroy(dev);

	LOGGING_DESTROY;
	return abs(exit_code);
}
