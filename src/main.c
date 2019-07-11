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
#include "http/server.h"
#ifdef WITH_GPIO
#	include "gpio.h"
#endif


static const char _SHORT_OPTS[] = "d:i:r:x:y:m:a:f:z:ntb:w:q:c:s:p:U:DM:k:e:lR:hv";
static const struct option _LONG_OPTS[] = {
	{"device",					required_argument,	NULL,	'd'},
	{"input",					required_argument,	NULL,	'i'},
	{"resolution",				required_argument,	NULL,	'r'},
	{"width",					required_argument,	NULL,	'x'},
	{"height",					required_argument,	NULL,	'y'},
	{"format",					required_argument,	NULL,	'm'},
	{"tv-standard",				required_argument,	NULL,	'a'},
	{"desired-fps",				required_argument,	NULL,	'f'},
	{"min-frame-size",			required_argument,	NULL,	'z'},
	{"persistent",				no_argument,		NULL,	'n'},
	{"dv-timings",				no_argument,		NULL,	't'},
	{"buffers",					required_argument,	NULL,	'b'},
	{"workers",					required_argument,	NULL,	'w'},
	{"quality",					required_argument,	NULL,	'q'},
	{"encoder",					required_argument,	NULL,	'c'},
	{"device-timeout",			required_argument,	NULL,	1000},
	{"device-error-delay",		required_argument,	NULL,	1001},

	{"brightness",				required_argument,	NULL,	2000},
	{"brightness-auto",			no_argument,		NULL,	2001},
	{"contrast",				required_argument,	NULL,	2002},
	{"saturation",				required_argument,	NULL,	2003},
	{"hue",						required_argument,	NULL,	2004},
	{"hue-auto",				no_argument,		NULL,	2005},
	{"gamma",					required_argument,	NULL,	2006},
	{"sharpness",				required_argument,	NULL,	2007},
	{"backlight-compensation",	required_argument,	NULL,	2008},
	{"white-balance",			required_argument,	NULL,	2009},
	{"white-balance-auto",		no_argument,		NULL,	2010},
	{"gain",					required_argument,	NULL,	2011},
	{"gain-auto",				no_argument,		NULL,	2012},

	{"host",					required_argument,	NULL,	's'},
	{"port",					required_argument,	NULL,	'p'},
	{"unix",					required_argument,	NULL,	'U'},
	{"unix-rm",					no_argument,		NULL,	'D'},
	{"unix-mode",				required_argument,	NULL,	'M'},
	{"user",					required_argument,	NULL,	3000},
	{"passwd",					required_argument,	NULL,	3001},
	{"static",					required_argument,	NULL,	3002},
	{"blank",					required_argument,	NULL,	'k'},
	{"drop-same-frames",		required_argument,	NULL,	'e'},
	{"slowdown",				no_argument,		NULL,	'l'},
	{"fake-resolution",			required_argument,	NULL,	'R'},
	{"fake-width",				required_argument,	NULL,	3003},
	{"fake-height",				required_argument,	NULL,	3004},
	{"server-timeout",			required_argument,	NULL,	3005},

#ifdef WITH_GPIO
	{"gpio-prog-running",		required_argument,	NULL,	4000},
	{"gpio-stream-online",		required_argument,	NULL,	4001},
	{"gpio-has-http-clients",	required_argument,	NULL,	4002},
	{"gpio-workers-busy-at",	required_argument,	NULL,	4003},
#endif

	{"perf",					no_argument,		NULL,	5000},
	{"verbose",					no_argument,		NULL,	5001},
	{"debug",					no_argument,		NULL,	5002},
	{"log-level",				required_argument,	NULL,	5010},
	{"help",					no_argument,		NULL,	'h'},
	{"version",					no_argument,		NULL,	'v'},
	{NULL, 0, NULL, 0},
};

static void _version(bool nl) {
	printf(VERSION);
#	ifdef WITH_OMX
	printf(" + OMX");
#	endif
	if (nl) {
		putchar('\n');
	}
}

static void _help(struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server) {
	printf("\nuStreamer - Lightweight and fast MJPG-HTTP streamer\n");
	printf("═══════════════════════════════════════════════════\n\n");
	printf("Version: ");
	_version(false);
	printf("; license: GPLv3\n");
	printf("Copyright (C) 2018 Maxim Devaev <mdevaev@gmail.com>\n\n");
	printf("Capturing options:\n");
	printf("══════════════════\n");
	printf("    -d|--device </dev/path>  ──────── Path to V4L2 device. Default: %s.\n\n", dev->path);
	printf("    -i|--input <N>  ───────────────── Input channel. Default: %u.\n\n", dev->input);
	printf("    -r|--resolution <WxH>  ────────── Initial image resolution. Default: %ux%u.\n\n", dev->width, dev->height);
	printf("    -m|--format <fmt>  ────────────── Image format.\n");
	printf("                                      Available: %s; default: YUYV.\n\n", FORMATS_STR);
	printf("    -a|--tv-standard <std>  ───────── Force TV standard.\n");
	printf("                                      Available: %s; default: disabled.\n\n", STANDARDS_STR);
	printf("    -f|--desired-fps <N>  ─────────── Desired FPS. Default: maximum possible.\n\n");
	printf("    -z|--min-frame-size <N>  ──────── Drop frames smaller then this limit. Useful if the device\n");
	printf("                                      produces small-sized garbage frames. Default: disabled.\n\n");
	printf("    -n|--persistent  ──────────────── Don't re-initialize device on timeout. Default: disabled.\n\n");
	printf("    -t|--dv-timings  ──────────────── Enable DV timings querying and events processing\n");
	printf("                                      to automatic resolution change. Default: disabled.\n\n");
	printf("    -b|--buffers <N>  ─────────────── The number of buffers to receive data from the device.\n");
	printf("                                      Each buffer may processed using an independent thread.\n");
	printf("                                      Default: %u (the number of CPU cores (but not more than 4) + 1).\n\n", dev->n_buffers);
	printf("    -w|--workers <N>  ─────────────── The number of worker threads but not more than buffers.\n");
	printf("                                      Default: %u (the number of CPU cores (but not more than 4)).\n\n", dev->n_workers);
	printf("    -q|--quality <N>  ─────────────── Set quality of JPEG encoding from 1 to 100 (best). Default: %u.\n\n", encoder->quality);
	printf("    -c|--encoder <type>  ──────────── Use specified encoder. It may affect the number of workers.\n");
	printf("                                      Available: %s; default: CPU.\n\n", ENCODER_TYPES_STR);
	printf("    --device-timeout <seconds>  ───── Timeout for device querying. Default: %u.\n\n", dev->timeout);
	printf("    --device-error-delay <seconds>  ─ Delay before trying to connect to the device again\n");
	printf("                                      after an error (timeout for example). Default: %u.\n\n", dev->error_delay);
	printf("Image control options:\n");
	printf("══════════════════════\n");
	printf("    --brightness <N>  ───────────── Set brightness. Default: no change.\n\n");
	printf("    --brightness-auto  ──────────── Enable automatic brightness control. Default: no change.\n\n");
	printf("    --contrast <N>  ─────────────── Set contrast. Default: no change.\n\n");
	printf("    --saturation <N>  ───────────── Set saturation. Default: no change.\n\n");
	printf("    --hue <N>  ──────────────────── Set hue. Default: no change.\n\n");
	printf("    --hue-auto  ─────────────────── Enable automatic hue control. Default: no change.\n\n");
	printf("    --gamma <N>  ────────────────── Set gamma. Default: no change.\n\n");
	printf("    --sharpness <N>  ────────────── Set sharpness. Default: no change.\n\n");
	printf("    --backlight-compensation <N>  ─ Set backlight compensation. Default: no change.\n\n");
	printf("    --white-balance <N>  ────────── Set white balance. Default: no change.\n\n");
	printf("    --white-balance-auto  ───────── Enable automatic white balance control. Default: no change.\n\n");
	printf("    --gain <N>  ─────────────────── Set gain. Default: no change.\n\n");
	printf("    --gain-auto  ────────────────── Enable automatic gain control. Default: no change.\n\n");
	printf("HTTP server options:\n");
	printf("════════════════════\n");
	printf("    -s|--host <address>  ──────── Listen on Hostname or IP. Default: %s.\n\n", server->host);
	printf("    -p|--port <N>  ────────────── Bind to this TCP port. Default: %u.\n\n", server->port);
	printf("    -U|--unix <path>  ─────────── Bind to UNIX domain socket. Default: disabled.\n\n");
	printf("    -D|--unix-rm  ─────────────── Try to remove old UNIX socket file before binding. Default: disabled.\n\n");
	printf("    -M|--unix-mode <mode>  ────── Set UNIX socket file permissions (like 777). Default: disabled.\n\n");
	printf("    --user <name>  ────────────── HTTP basic auth user. Default: disabled.\n\n");
	printf("    --passwd <str>  ───────────── HTTP basic auth passwd. Default: empty.\n\n");
	printf("    --static <path> ───────────── Path to dir with static files instead of embedded root index page.\n");
	printf("                                  Symlinks are not supported for security reasons. Default: disabled.\n\n");
	printf("    -k|--blank <path> ─────────── Path to JPEG file that will be shown when the device is disconnected\n");
	printf("                                  during the streaming. Default: black screen 640x480 with 'NO SIGNAL'.\n\n");
	printf("    -e|--drop-same-frames <N>  ── Don't send identical frames to clients, but no more than specified number.\n");
	printf("                                  It can significantly reduce the outgoing traffic, but will increase\n");
	printf("                                  the CPU loading. Don't use this option with analog signal sources\n");
	printf("                                  or webcams, it's useless. Default: disabled.\n\n");
	printf("    -l|--slowdown  ────────────── Slowdown capturing to 1 FPS or less when no stream clients are connected.\n");
	printf("                                  Useful to reduce CPU consumption. Default: disabled.\n\n");
	printf("    -R|--fake-resolution <WxH>  ─ Override image resolution for state. Default: disabled.\n\n");
	printf("    --server-timeout <seconds>  ─ Timeout for client connections. Default: %u.\n\n", server->timeout);
#ifdef WITH_GPIO
	printf("GPIO options:\n");
	printf("═════════════\n");
	printf("    --gpio-prog-running <pin>  ───── Set 1 on GPIO pin while uStreamer is running. Default: disabled.\n\n");
	printf("    --gpio-stream-online <pin>  ──── Set 1 while streaming. Default: disabled\n\n");
	printf("    --gpio-has-http-clients <pin>  ─ Set 1 while stream has at least one client. Default: disabled.\n\n");
	printf("    --gpio-workers-busy-at <pin>  ── Set 1 on (pin + N) while worker with number N has a job.\n");
	printf("                                     The worker's numbering starts from 0. Default: disabled\n\n");
#endif
	printf("Misc options:\n");
	printf("═════════════\n");
	printf("    --log-level <N>  ─ Verbosity level of messages from 0 (info) to 3 (debug).\n");
	printf("                       Enabling debugging messages can slow down the program.\n");
	printf("                       Available levels: 0 (info), 1 (performance), 2 (verbose), 3 (debug).\n");
	printf("                       Default: %u.\n\n", log_level);
	printf("    --perf  ────────── Enable performance messages (same as --log-level=1). Default: disabled.\n\n");
	printf("    --verbose  ─────── Enable verbose messages and lower (same as --log-level=2). Default: disabled.\n\n");
	printf("    --debug  ───────── Enable debug messages and lower (same as --log-level=3). Default: disabled.\n\n");
	printf("    -h|--help  ─────── Print this text and exit.\n\n");
	printf("    -v|--version  ──── Print version and exit.\n\n");
}

static int _parse_options(int argc, char *argv[], struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server) {
#	define OPT_SET(_dest, _value) \
		{ _dest = _value; break; }

#	define OPT_UNSIGNED(_dest, _name, _min, _max) { \
			errno = 0; char *_end = NULL; int _tmp = strtol(optarg, &_end, 0); \
			if (errno || *_end || _tmp < _min || _tmp > _max) { \
				printf("Invalid value for '%s=%s': min=%u, max=%u\n", _name, optarg, _min, _max); \
				return -1; \
			} \
			_dest = _tmp; \
			break; \
		}

#	define OPT_RESOLUTION_OBSOLETE(_dest, _name, _replace, _min, _max) { \
			printf("\n=== WARNING! The option '%s' is obsolete; use '%s' instead it ===\n\n", _name, _replace); \
			OPT_UNSIGNED(_dest, _name, _min, _max); \
		}

#	define OPT_RESOLUTION(_dest_width, _dest_height, _name, _min_width, _min_height) { \
			int _tmp_width, _tmp_height; \
			if (sscanf(optarg, "%dx%d", &_tmp_width, &_tmp_height) != 2) { \
				printf("Invalid value for '%s=%s'\n", _name, optarg); \
				return -1; \
			} \
			if (_tmp_width < _min_width || _tmp_width > VIDEO_MAX_WIDTH) { \
				printf("Invalid width of '%s=%s': min=%u, max=%u\n", _name, optarg, _min_width, VIDEO_MAX_WIDTH); \
				return -1; \
			} \
			if (_tmp_height < _min_height || _tmp_height > VIDEO_MAX_HEIGHT) { \
				printf("Invalid height of '%s=%s': min=%u, max=%u\n", _name, optarg, _min_height, VIDEO_MAX_HEIGHT); \
				return -1; \
			} \
			_dest_width = _tmp_width; \
			_dest_height = _tmp_height; \
			break; \
		}

#	define OPT_PARSE(_dest, _func, _invalid, _name) { \
			if ((_dest = _func(optarg)) == _invalid) { \
				printf("Unknown " _name ": %s\n", optarg); \
				return -1; \
			} \
			break; \
		}

#	define OPT_INT(_dest, _name, _base) { \
			errno = 0; char *_end = NULL; int _tmp = strtol(optarg, &_end, _base); \
			if (errno || *_end) { \
				printf("Invalid value for '%s=%s'\n", _name, optarg); \
				return -1; \
			} \
			_dest = _tmp; \
			break; \
		}

#	define OPT_CHMOD(_dest, _name) \
		OPT_INT(_dest, _name, 8)

#	define OPT_CTL(_dest) { \
			dev->ctl->_dest.value_set = true; \
			dev->ctl->_dest.auto_set = false; \
			OPT_INT(dev->ctl->_dest.value, "--"#_dest, 10); \
			break; \
		}

#	define OPT_CTL_AUTO(_dest) { \
			dev->ctl->_dest.value_set = false; \
			dev->ctl->_dest.auto_set = true; \
			break; \
		}

	int index;
	int ch;

	log_level = LOG_LEVEL_INFO;
	while ((ch = getopt_long(argc, argv, _SHORT_OPTS, _LONG_OPTS, &index)) >= 0) {
		switch (ch) {
			case 'd':	OPT_SET(dev->path, optarg);
			case 'i':	OPT_UNSIGNED(dev->input, "--input", 0, 128);
			case 'r':	OPT_RESOLUTION(dev->width, dev->height, "--resolution", VIDEO_MIN_WIDTH, VIDEO_MIN_HEIGHT);
			case 'x':	OPT_RESOLUTION_OBSOLETE(dev->width, "--width", "--resolution", VIDEO_MIN_WIDTH, VIDEO_MAX_WIDTH);
			case 'y':	OPT_RESOLUTION_OBSOLETE(dev->height, "--height", "--resolution", VIDEO_MIN_HEIGHT, VIDEO_MAX_HEIGHT);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case 'm':	OPT_PARSE(dev->format, device_parse_format, FORMAT_UNKNOWN, "pixel format");
#			pragma GCC diagnostic pop
			case 'a':	OPT_PARSE(dev->standard, device_parse_standard, STANDARD_UNKNOWN, "TV standard");
			case 'f':	OPT_UNSIGNED(dev->desired_fps, "--desired-fps", 0, 30);
			case 'z':	OPT_UNSIGNED(dev->min_frame_size, "--min-frame-size", 0, 8192);
			case 'n':	OPT_SET(dev->persistent, true);
			case 't':	OPT_SET(dev->dv_timings, true);
			case 'b':	OPT_UNSIGNED(dev->n_buffers, "--buffers", 1, 32);
			case 'w':	OPT_UNSIGNED(dev->n_workers, "--workers", 1, 32);
			case 'q':	OPT_UNSIGNED(encoder->quality, "--quality", 1, 100);
			case 'c':	OPT_PARSE(encoder->type, encoder_parse_type, ENCODER_TYPE_UNKNOWN, "encoder type");
			case 1000:	OPT_UNSIGNED(dev->timeout, "--device-timeout", 1, 60);
			case 1001:	OPT_UNSIGNED(dev->error_delay, "--device-error-delay", 1, 60);

			case 2000:	OPT_CTL(brightness);
			case 2001:	OPT_CTL_AUTO(brightness);
			case 2002:	OPT_CTL(contrast);
			case 2003:	OPT_CTL(saturation);
			case 2004:	OPT_CTL(hue);
			case 2005:	OPT_CTL_AUTO(hue);
			case 2006:	OPT_CTL(gamma);
			case 2007:	OPT_CTL(sharpness);
			case 2008:	OPT_CTL(backlight_compensation);
			case 2009:	OPT_CTL(white_balance);
			case 2010:	OPT_CTL_AUTO(white_balance);
			case 2011:	OPT_CTL(gain);
			case 2012:	OPT_CTL_AUTO(gain);

			case 's':	OPT_SET(server->host, optarg);
			case 'p':	OPT_UNSIGNED(server->port, "--port", 1, 65535);
			case 'U':	OPT_SET(server->unix_path, optarg);
			case 'D':	OPT_SET(server->unix_rm, true);
			case 'M':	OPT_CHMOD(server->unix_mode, "--unix-mode");
			case 3000:	OPT_SET(server->user, optarg);
			case 3001:	OPT_SET(server->passwd, optarg);
			case 3002:	OPT_SET(server->static_path, optarg);
			case 'k':	OPT_SET(server->blank_path, optarg);
			case 'e':	OPT_UNSIGNED(server->drop_same_frames, "--drop-same-frames", 0, 30);
			case 'l':	OPT_SET(server->slowdown, true);
			case 'R':	OPT_RESOLUTION(server->fake_width, server->fake_height, "--fake-resolution", 0, 0);
			case 3003:	OPT_RESOLUTION_OBSOLETE(server->fake_width, "--fake-width", "--fake-resolution", 0, VIDEO_MAX_WIDTH);
			case 3004:	OPT_RESOLUTION_OBSOLETE(server->fake_height, "--fake-height", "--fake-resolution", 0, VIDEO_MAX_HEIGHT);
			case 3005:	OPT_UNSIGNED(server->timeout, "--server-timeout", 1, 60);

#			ifdef WITH_GPIO
			case 4000:	OPT_UNSIGNED(gpio_pin_prog_running, "--gpio-prog-running", 0, 256);
			case 4001:	OPT_UNSIGNED(gpio_pin_stream_online, "--gpio-stream-online", 0, 256);
			case 4002:	OPT_UNSIGNED(gpio_pin_has_http_clients, "--gpio-has-http-clients", 0, 256);
			case 4003:	OPT_UNSIGNED(gpio_pin_workers_busy_at, "--gpio-workers-busy-at", 0, 256);
#			endif

			case 5000:	OPT_SET(log_level, LOG_LEVEL_PERF);
			case 5001:	OPT_SET(log_level, LOG_LEVEL_VERBOSE);
			case 5002:	OPT_SET(log_level, LOG_LEVEL_DEBUG);
			case 5010:	OPT_UNSIGNED(log_level, "--log-level", 0, 3);
			case 'h':	_help(dev, encoder, server); return 1;
			case 'v':	_version(true); return 1;
			case 0:		break;
			default:	_help(dev, encoder, server); return -1;
		}
	}

#	undef OPT_CTL_AUTO
#	undef OPT_CTL
#	undef OPT_CHMOD
#	undef OPT_INT
#	undef OPT_PARSE
#	undef OPT_RESOLUTION
#	undef OPT_UNSIGNED
#	undef OPT_SET
	return 0;
}

struct main_context_t {
	struct stream_t			*stream;
	struct http_server_t	*server;
};

static struct main_context_t *_ctx;

static void _block_thread_signals(void) {
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

static void _install_signal_handlers(void) {
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

#	ifdef WITH_GPIO
	GPIO_INIT;
#	endif

	dev = device_init();
	encoder = encoder_init();
	stream = stream_init(dev, encoder);
	server = http_server_init(stream);

	if ((exit_code = _parse_options(argc, argv, dev, encoder, server)) == 0) {
#		ifdef WITH_GPIO
		GPIO_INIT_PINOUT;
#		endif

		_install_signal_handlers();

		pthread_t stream_loop_tid;
		pthread_t server_loop_tid;
		struct main_context_t ctx;

		ctx.stream = stream;
		ctx.server = server;
		_ctx = &ctx;

		if ((exit_code = http_server_listen(server)) == 0) {
#			ifdef WITH_GPIO
			GPIO_SET_HIGH(prog_running);
#			endif

			A_THREAD_CREATE(&stream_loop_tid, _stream_loop_thread, NULL);
			A_THREAD_CREATE(&server_loop_tid, _server_loop_thread, NULL);
			A_THREAD_JOIN(server_loop_tid);
			A_THREAD_JOIN(stream_loop_tid);
		}
	}

	http_server_destroy(server);
	stream_destroy(stream);
	encoder_destroy(encoder);
	device_destroy(dev);

#	ifdef WITH_GPIO
	GPIO_SET_LOW(prog_running);
#	endif

	LOGGING_DESTROY;
	return (exit_code < 0 ? 1 : 0);
}
