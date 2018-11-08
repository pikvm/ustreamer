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
#include <stdbool.h>
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


static const char _short_opts[] = "d:i:x:y:f:a:e:z:tn:w:q:c:s:p:r:h";
static const struct option _long_opts[] = {
	{"device",					required_argument,	NULL,	'd'},
	{"input",					required_argument,	NULL,	'i'},
	{"width",					required_argument,	NULL,	'x'},
	{"height",					required_argument,	NULL,	'y'},
	{"format",					required_argument,	NULL,	'm'},
	{"tv-standard",				required_argument,	NULL,	'a'},
	{"fps",						required_argument,	NULL,	'f'},
	{"every-frame",				required_argument,	NULL,	'e'},
	{"min-frame-size",			required_argument,	NULL,	'z'},
	{"dv-timings",				no_argument,		NULL,	't'},
	{"buffers",					required_argument,	NULL,	'b'},
	{"workers",					required_argument,	NULL,	'w'},
	{"quality",					required_argument,	NULL,	'q'},
	{"encoder",					required_argument,	NULL,	'c'},
#	ifdef OMX_ENCODER
	{"encoder-omx-use-ijg",		required_argument,	NULL,	500},
#	endif
	{"device-timeout",			required_argument,	NULL,	1000},
	{"device-persistent",		no_argument,		NULL,	1001},
	{"device-error-delay",		required_argument,	NULL,	1002},

	{"host",					required_argument,	NULL,	's'},
	{"port",					required_argument,	NULL,	'p'},
	{"drop-same-frames",		required_argument,	NULL,	'r'},
	{"fake-width",				required_argument,	NULL,	2001},
	{"fake-height",				required_argument,	NULL,	2002},
	{"server-timeout",			required_argument,	NULL,	2003},

	{"perf",					no_argument,		NULL,	5000},
	{"verbose",					no_argument,		NULL,	5001},
	{"debug",					no_argument,		NULL,	5002},
	{"log-level",				required_argument,	NULL,	5010},
	{"help",					no_argument,		NULL,	'h'},
	{"version",					no_argument,		NULL,	6000},
	{NULL, 0, NULL, 0},
};

static void _version(bool nl) {
	printf(VERSION);
#	ifdef OMX_ENCODER
	printf(" + OMX");
#	endif
	if (nl) {
		putchar('\n');
	}
}

static void _help(struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server) {
	printf("\nuStreamer - Lightweight and fast MJPG-HTTP streamer\n");
	printf("===================================================\n\n");
	printf("Version: ");
	_version(false);
	printf("; license: GPLv3\n");
	printf("Copyright (C) 2018 Maxim Devaev <mdevaev@gmail.com>\n\n");
	printf("Capturing options:\n");
	printf("------------------\n");
	printf("    -d|--device </dev/path>          -- Path to V4L2 device. Default: %s.\n\n", dev->path);
	printf("    -i|--input <N>                   -- Input channel. Default: %u.\n\n", dev->input);
	printf("    -x|--width <N>                   -- Initial image width. Default: %d.\n\n", dev->width);
	printf("    -y|--height <N>                  -- Initial image height. Default: %d.\n\n", dev->height);
	printf("    -m|--format <fmt>                -- Image format.\n");
	printf("                                        Available: %s; default: YUYV.\n\n", FORMATS_STR);
	printf("    -a|--tv-standard <std>           -- Force TV standard.\n");
	printf("                                        Available: %s; default: disabled.\n\n", STANDARDS_STR);
	printf("    -f|--desired-fps <N>             -- Desired FPS; default: maximum as possible.\n\n");
	printf("    -e|--every-frame <N>             -- Drop all input frames except specified. Default: disabled.\n\n");
	printf("    -z|--min-frame-size <N>          -- Drop frames smaller then this limit.\n");
	printf("                                        Useful if the device produces small-sized garbage frames.\n\n");
	printf("    -t|--dv-timings                  -- Enable DV timings queriyng and events processing.\n");
	printf("                                        Supports automatic resolution changing. Default: disabled.\n\n");
	printf("    -b|--buffers <N>                 -- The number of buffers to receive data from the device.\n");
	printf("                                        Each buffer may processed using an intermediate thread.\n");
	printf("                                        Default: %d (number of CPU cores + 1)\n\n", dev->n_buffers);
	printf("    -w|--workers <N>                 -- The number of compressing threads. Default: %d (== --buffers).\n\n", dev->n_workers);
	printf("    -q|--quality <N>                 -- Set quality of JPEG encoding from 1 to 100 (best). Default: %d.\n\n", encoder->quality);
	printf("    --encoder <type>                 -- Use specified encoder. It may affects to workers number.\n");
	printf("                                     -- Available: %s; default: CPU.\n\n", ENCODER_TYPES_STR);
#	ifdef OMX_ENCODER
	printf("    --encoder-omx-use-ijg            -- Use the standard IJG quality tables when encoding images using OMX.\n");
	printf("                                        Default: disabled.\n\n");
#	endif
	printf("    --device-timeout <seconds>       -- Timeout for device querying. Default: %d\n\n", dev->timeout);
	printf("    --device-persistent              -- Don't re-initialize device on timeout. Default: disabled.\n\n");
	printf("    --device-error-delay <seconds>   -- Delay before trying to connect to the device again\n");
	printf("                                        after a timeout. Default: %d\n\n", dev->error_delay);
	printf("HTTP server options:\n");
	printf("--------------------\n");
	printf("    --host <address>           -- Listen on Hostname or IP. Default: %s\n\n", server->host);
	printf("    --port <N>                 -- Bind to this TCP port. Default: %d\n\n", server->port);
	printf("    --drop-same-frames <N>     -- Don't send same frames to clients, but no more than specified number.\n");
	printf("                                  It can significantly reduce the outgoing traffic, but will increase\n");
	printf("                                  the CPU loading. Don't use this option with analog signal sources\n");
	printf("                                  or webcams, it's useless. Default: disabled.\n\n");
	printf("    --fake-width <N>           -- Override image width for /state. Default: disabled\n\n");
	printf("    --fake-height <N>          -- Override image height for /state. Default: disabled.\n\n");
	printf("    --server-timeout <seconds> -- Timeout for client connections. Default: %d\n\n", server->timeout);
	printf("Misc options:\n");
	printf("-------------\n");
	printf("    --log-level <N> -- Verbosity level of messages from 0 (info) to 3 (debug).\n");
	printf("                       Enabling debugging messages can slow down the program.\n");
	printf("                       Available levels: 0=info, 1=performance, 2=verbose, 3=debug.\n");
	printf("                       Default: %d.\n\n", log_level);
	printf("    --perf          -- Enable performance messages (same as log-level=1). Default: disabled.\n\n");
	printf("    --verbose       -- Enable verbose messages and lower (same as log-level=2). Default: disabled.\n\n");
	printf("    --debug         -- Enable debug messages and lower (same as --log-level=3). Default: disabled.\n\n");
	printf("    -h|--help       -- Print this messages and exit.\n\n");
}

static int _parse_options(int argc, char *argv[], struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server) {
#	define OPT_SET(_dest, _value) \
		{ _dest = _value; break; }

#	define OPT_UNSIGNED(_dest, _name, _min, _max) \
		{ errno = 0; int _tmp = strtol(optarg, NULL, 0); \
		if (errno || _tmp < _min || _tmp > _max) \
		{ printf("Invalid value for '%s=%u'; min=%u; max=%u\n", _name, _tmp, _min, _max); return -1; } \
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
			case 'd':	OPT_SET(dev->path, optarg);
			case 'i':	OPT_UNSIGNED(dev->input, "--input", 0, 128);
			case 'x':	OPT_UNSIGNED(dev->width, "--width", 320, 1920);
			case 'y':	OPT_UNSIGNED(dev->height, "--height", 180, 1200);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case 'm':	OPT_PARSE(dev->format, device_parse_format, FORMAT_UNKNOWN, "pixel format");
#			pragma GCC diagnostic pop
			case 'a':	OPT_PARSE(dev->standard, device_parse_standard, STANDARD_UNKNOWN, "TV standard");
			case 'f':	OPT_UNSIGNED(dev->desired_fps, "--desired-fps", 0, 30);
			case 'e':	OPT_UNSIGNED(dev->every_frame, "--every-frame", 1, 30);
			case 'z':	OPT_UNSIGNED(dev->min_frame_size, "--min-frame-size", 0, 8192);
			case 't':	OPT_SET(dev->dv_timings, true);
			case 'b':	OPT_UNSIGNED(dev->n_buffers, "--buffers", 1, 32);
			case 'w':	OPT_UNSIGNED(dev->n_workers, "--workers", 1, 32);
			case 'q':	OPT_UNSIGNED(encoder->quality, "--quality", 1, 100);
			case 'c':	OPT_PARSE(encoder->type, encoder_parse_type, ENCODER_TYPE_UNKNOWN, "encoder type");
#			ifdef OMX_ENCODER
			case 500:	OPT_SET(encoder->omx_use_ijg, true);
#			endif
			case 1000:	OPT_UNSIGNED(dev->timeout, "--device-timeout", 1, 60);
			case 1001:	OPT_SET(dev->persistent, true);
			case 1002:	OPT_UNSIGNED(dev->error_delay, "--device-error-delay", 1, 60);

			case 's':	OPT_SET(server->host, optarg);
			case 'p':	OPT_UNSIGNED(server->port, "--port", 1, 65535);
			case 'r':	OPT_UNSIGNED(server->drop_same_frames, "--drop-same-frames", 0, 30);
			case 2001:	OPT_UNSIGNED(server->fake_width, "--fake-width", 0, 1920);
			case 2002:	OPT_UNSIGNED(server->fake_height, "--fake-height", 0, 1200);
			case 2003:	OPT_UNSIGNED(server->timeout, "--server-timeout", 1, 60);

			case 5000:	OPT_SET(log_level, LOG_LEVEL_PERF);
			case 5001:	OPT_SET(log_level, LOG_LEVEL_VERBOSE);
			case 5002:	OPT_SET(log_level, LOG_LEVEL_DEBUG);
			case 5010:	OPT_UNSIGNED(log_level, "--log-level", 0, 3);
			case 'h':	_help(dev, encoder, server); return 1;
			case 6000:	_version(true); return 1;
			case 0:		break;
			default:	_help(dev, encoder, server); return -1;
		}
	}

#	undef OPT_PARSE
#	undef OPT_UNSIGNED
#	undef OPT_SET
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
	encoder = encoder_init();
	stream = stream_init(dev, encoder);
	server = http_server_init(stream);

	if ((exit_code = _parse_options(argc, argv, dev, encoder, server)) == 0) {
		_install_signal_handlers();
		encoder_prepare(encoder, dev);

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
	return (exit_code < 0 ? 1 : 0);
}
