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


#include "options.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <assert.h>

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


enum _OPT_VALUES {
	_O_DEVICE = 'd',
	_O_INPUT = 'i',
	_O_RESOLUTION = 'r',
	_O_WIDTH = 'x',
	_O_HEIGHT = 'y',
	_O_FORMAT = 'm',
	_O_TV_STANDARD = 'a',
	_O_DESIRED_FPS = 'f',
	_O_MIN_FRAME_SIZE = 'z',
	_O_PERSISTENT = 'n',
	_O_DV_TIMINGS = 't',
	_O_BUFFERS = 'b',
	_O_WORKERS = 'w',
	_O_QUALITY = 'q',
	_O_ENCODER = 'c',
#ifdef WITH_OMX
	_O_GLITCHED_RESOLUTIONS = 'g',
#endif

	_O_HOST = 's',
	_O_PORT = 'p',
	_O_UNIX = 'U',
	_O_UNIX_RM = 'D',
	_O_UNIX_MODE = 'M',
	_O_BLANK = 'k',
	_O_DROP_SAME_FRAMES = 'e',
	_O_SLOWDOWN = 'l',
	_O_FAKE_RESOLUTION = 'R',

	_O_HELP = 'h',
	_O_VERSION = 'v',

	// Longs only

	_O_DEVICE_TIMEOUT = 10000,
	_O_DEVICE_ERROR_DELAY,

	_O_BRIGHTNESS,
	_O_BRIGHTNESS_AUTO,
	_O_CONTRAST,
	_O_SATURATION,
	_O_HUE,
	_O_HUE_AUTO,
	_O_GAMMA,
	_O_SHARPNESS,
	_O_BACKLIGHT_COMPENSATION,
	_O_WHITE_BALANCE,
	_O_WHITE_BALANCE_AUTO,
	_O_GAIN,
	_O_GAIN_AUTO,

	_O_USER,
	_O_PASSWD,
	_O_STATIC,
	_O_FAKE_WIDTH,
	_O_FAKE_HEIGHT,
	_O_SERVER_TIMEOUT,

#ifdef WITH_GPIO
	_O_GPIO_PROG_RUNNING,
	_O_GPIO_STREAM_ONLINE,
	_O_GPIO_HAS_HTTP_CLIENTS,
	_O_GPIO_WORKERS_BUSY_AT,
#endif

	_O_LOG_LEVEL,
	_O_PERF,
	_O_VERBOSE,
	_O_DEBUG,
	_O_FORCE_COLORS,
	_O_NO_COLORS,
};

static const struct option _LONG_OPTS[] = {
	{"device",					required_argument,	NULL,	_O_DEVICE},
	{"input",					required_argument,	NULL,	_O_INPUT},
	{"resolution",				required_argument,	NULL,	_O_RESOLUTION},
	{"width",					required_argument,	NULL,	_O_WIDTH},
	{"height",					required_argument,	NULL,	_O_HEIGHT},
	{"format",					required_argument,	NULL,	_O_FORMAT},
	{"tv-standard",				required_argument,	NULL,	_O_TV_STANDARD},
	{"desired-fps",				required_argument,	NULL,	_O_DESIRED_FPS},
	{"min-frame-size",			required_argument,	NULL,	_O_MIN_FRAME_SIZE},
	{"persistent",				no_argument,		NULL,	_O_PERSISTENT},
	{"dv-timings",				no_argument,		NULL,	_O_DV_TIMINGS},
	{"buffers",					required_argument,	NULL,	_O_BUFFERS},
	{"workers",					required_argument,	NULL,	_O_WORKERS},
	{"quality",					required_argument,	NULL,	_O_QUALITY},
	{"encoder",					required_argument,	NULL,	_O_ENCODER},
#	ifdef WITH_OMX
	{"glitched-resolutions",	required_argument,	NULL,	_O_GLITCHED_RESOLUTIONS},
#	endif
	{"device-timeout",			required_argument,	NULL,	_O_DEVICE_TIMEOUT},
	{"device-error-delay",		required_argument,	NULL,	_O_DEVICE_ERROR_DELAY},

	{"brightness",				required_argument,	NULL,	_O_BRIGHTNESS},
	{"brightness-auto",			no_argument,		NULL,	_O_BRIGHTNESS_AUTO},
	{"contrast",				required_argument,	NULL,	_O_CONTRAST},
	{"saturation",				required_argument,	NULL,	_O_SATURATION},
	{"hue",						required_argument,	NULL,	_O_HUE},
	{"hue-auto",				no_argument,		NULL,	_O_HUE_AUTO},
	{"gamma",					required_argument,	NULL,	_O_GAMMA},
	{"sharpness",				required_argument,	NULL,	_O_SHARPNESS},
	{"backlight-compensation",	required_argument,	NULL,	_O_BACKLIGHT_COMPENSATION},
	{"white-balance",			required_argument,	NULL,	_O_WHITE_BALANCE},
	{"white-balance-auto",		no_argument,		NULL,	_O_WHITE_BALANCE_AUTO},
	{"gain",					required_argument,	NULL,	_O_GAIN},
	{"gain-auto",				no_argument,		NULL,	_O_GAIN_AUTO},

	{"host",					required_argument,	NULL,	_O_HOST},
	{"port",					required_argument,	NULL,	_O_PORT},
	{"unix",					required_argument,	NULL,	_O_UNIX},
	{"unix-rm",					no_argument,		NULL,	_O_UNIX_RM},
	{"unix-mode",				required_argument,	NULL,	_O_UNIX_MODE},
	{"user",					required_argument,	NULL,	_O_USER},
	{"passwd",					required_argument,	NULL,	_O_PASSWD},
	{"static",					required_argument,	NULL,	_O_STATIC},
	{"blank",					required_argument,	NULL,	_O_BLANK},
	{"drop-same-frames",		required_argument,	NULL,	_O_DROP_SAME_FRAMES},
	{"slowdown",				no_argument,		NULL,	_O_SLOWDOWN},
	{"fake-resolution",			required_argument,	NULL,	_O_FAKE_RESOLUTION},
	{"fake-width",				required_argument,	NULL,	_O_FAKE_WIDTH},
	{"fake-height",				required_argument,	NULL,	_O_FAKE_HEIGHT},
	{"server-timeout",			required_argument,	NULL,	_O_SERVER_TIMEOUT},

#ifdef WITH_GPIO
	{"gpio-prog-running",		required_argument,	NULL,	_O_GPIO_PROG_RUNNING},
	{"gpio-stream-online",		required_argument,	NULL,	_O_GPIO_STREAM_ONLINE},
	{"gpio-has-http-clients",	required_argument,	NULL,	_O_GPIO_HAS_HTTP_CLIENTS},
	{"gpio-workers-busy-at",	required_argument,	NULL,	_O_GPIO_WORKERS_BUSY_AT},
#endif

	{"log-level",				required_argument,	NULL,	_O_LOG_LEVEL},
	{"perf",					no_argument,		NULL,	_O_PERF},
	{"verbose",					no_argument,		NULL,	_O_VERBOSE},
	{"debug",					no_argument,		NULL,	_O_DEBUG},
	{"force-colors",			no_argument,		NULL,	_O_FORCE_COLORS},
	{"no-colors",				no_argument,		NULL,	_O_NO_COLORS},

	{"help",					no_argument,		NULL,	_O_HELP},
	{"version",					no_argument,		NULL,	_O_VERSION},

	{NULL, 0, NULL, 0},
};


static int _parse_resolution(const char *str, unsigned *width, unsigned *height, bool limited);
#ifdef WITH_OMX
static int _parse_glitched_resolutions(const char *str, struct encoder_t *encoder);
#endif

static void _version(bool nl);
static void _help(struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server);


int parse_options(int argc, char *argv[], struct device_t *dev, struct encoder_t *encoder, struct http_server_t *server) {
#	define OPT_SET(_dest, _value) { \
			_dest = _value; \
			break; \
		}

#	define OPT_NUMBER(_name, _dest, _min, _max, _base) { \
			errno = 0; char *_end = NULL; long long _tmp = strtoll(optarg, &_end, _base); \
			if (errno || *_end || _tmp < _min || _tmp > _max) { \
				printf("Invalid value for '%s=%s': min=%u, max=%u\n", _name, optarg, _min, _max); \
				return -1; \
			} \
			_dest = _tmp; \
			break; \
		}

#	define OPT_RESOLUTION(_name, _dest_width, _dest_height, _limited) { \
			switch (_parse_resolution(optarg, &_dest_width, &_dest_height, _limited)) { \
				case -1: \
					printf("Invalid resolution format for '%s=%s'\n", _name, optarg); \
					return -1; \
				case -2: \
					printf("Invalid width of '%s=%s': min=%u, max=%u\n", _name, optarg, VIDEO_MIN_WIDTH, VIDEO_MAX_WIDTH); \
					return -1; \
				case -3: \
					printf("Invalid height of '%s=%s': min=%u, max=%u\n", _name, optarg, VIDEO_MIN_HEIGHT, VIDEO_MAX_HEIGHT); \
					return -1; \
				case 0: break; \
				default: assert(0 && "Unknown error"); \
			} \
			break; \
		}

#	define OPT_RESOLUTION_OBSOLETE(_name, _replace, _dest, _min, _max) { \
			printf("\n=== WARNING! The option '%s' is obsolete; use '%s' instead it ===\n\n", _name, _replace); \
			OPT_NUMBER(_name, _dest, _min, _max, 0); \
		}

#	ifdef WITH_OMX
#		define OPT_GLITCHED_RESOLUTIONS { \
				if (_parse_glitched_resolutions(optarg, encoder) < 0) { \
					return -1; \
				} \
				break; \
			}
#	endif

#	define OPT_PARSE(_name, _dest, _func, _invalid) { \
			if ((_dest = _func(optarg)) == _invalid) { \
				printf("Unknown " _name ": %s\n", optarg); \
				return -1; \
			} \
			break; \
		}

#	define OPT_CTL(_dest) { \
			dev->ctl._dest.value_set = true; \
			dev->ctl._dest.auto_set = false; \
			OPT_NUMBER("--"#_dest, dev->ctl._dest.value, INT_MIN, INT_MAX, 0); \
			break; \
		}

#	define OPT_CTL_AUTO(_dest) { \
			dev->ctl._dest.value_set = false; \
			dev->ctl._dest.auto_set = true; \
			break; \
		}

	int ch;
	int short_index;
	int opt_index;
	char short_opts[1024] = {0};

	for (short_index = 0, opt_index = 0; _LONG_OPTS[opt_index].name != NULL; ++opt_index) {
		if (isalpha(_LONG_OPTS[opt_index].val)) {
			short_opts[short_index] = _LONG_OPTS[opt_index].val;
			++short_index;
		}
		if (_LONG_OPTS[opt_index].has_arg == required_argument) {
			short_opts[short_index] = ':';
			++short_index;
		}
	}

	while ((ch = getopt_long(argc, argv, short_opts, _LONG_OPTS, NULL)) >= 0) {
		switch (ch) {
			case _O_DEVICE:			OPT_SET(dev->path, optarg);
			case _O_INPUT:			OPT_NUMBER("--input", dev->input, 0, 128, 0);
			case _O_RESOLUTION:		OPT_RESOLUTION("--resolution", dev->width, dev->height, true);
			case _O_WIDTH:			OPT_RESOLUTION_OBSOLETE("--width", "--resolution", dev->width, VIDEO_MIN_WIDTH, VIDEO_MAX_WIDTH);
			case _O_HEIGHT:			OPT_RESOLUTION_OBSOLETE("--height", "--resolution", dev->height, VIDEO_MIN_HEIGHT, VIDEO_MAX_HEIGHT);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case _O_FORMAT:			OPT_PARSE("pixel format", dev->format, device_parse_format, FORMAT_UNKNOWN);
#			pragma GCC diagnostic pop
			case _O_TV_STANDARD:	OPT_PARSE("TV standard", dev->standard, device_parse_standard, STANDARD_UNKNOWN);
			case _O_DESIRED_FPS:	OPT_NUMBER("--desired-fps", dev->desired_fps, 0, VIDEO_MAX_FPS, 0);
			case _O_MIN_FRAME_SIZE:	OPT_NUMBER("--min-frame-size", dev->min_frame_size, 0, 8192, 0);
			case _O_PERSISTENT:		OPT_SET(dev->persistent, true);
			case _O_DV_TIMINGS:		OPT_SET(dev->dv_timings, true);
			case _O_BUFFERS:		OPT_NUMBER("--buffers", dev->n_buffers, 1, 32, 0);
			case _O_WORKERS:		OPT_NUMBER("--workers", dev->n_workers, 1, 32, 0);
			case _O_QUALITY:		OPT_NUMBER("--quality", encoder->quality, 1, 100, 0);
			case _O_ENCODER:		OPT_PARSE("encoder type", encoder->type, encoder_parse_type, ENCODER_TYPE_UNKNOWN);
#			ifdef WITH_OMX
			case _O_GLITCHED_RESOLUTIONS:	OPT_GLITCHED_RESOLUTIONS;
#			endif
			case _O_DEVICE_TIMEOUT:		OPT_NUMBER("--device-timeout", dev->timeout, 1, 60, 0);
			case _O_DEVICE_ERROR_DELAY:	OPT_NUMBER("--device-error-delay", dev->error_delay, 1, 60, 0);

			case _O_BRIGHTNESS:				OPT_CTL(brightness);
			case _O_BRIGHTNESS_AUTO:		OPT_CTL_AUTO(brightness);
			case _O_CONTRAST:				OPT_CTL(contrast);
			case _O_SATURATION:				OPT_CTL(saturation);
			case _O_HUE:					OPT_CTL(hue);
			case _O_HUE_AUTO:				OPT_CTL_AUTO(hue);
			case _O_GAMMA:					OPT_CTL(gamma);
			case _O_SHARPNESS:				OPT_CTL(sharpness);
			case _O_BACKLIGHT_COMPENSATION:	OPT_CTL(backlight_compensation);
			case _O_WHITE_BALANCE:			OPT_CTL(white_balance);
			case _O_WHITE_BALANCE_AUTO:		OPT_CTL_AUTO(white_balance);
			case _O_GAIN:					OPT_CTL(gain);
			case _O_GAIN_AUTO:				OPT_CTL_AUTO(gain);

			case _O_HOST:				OPT_SET(server->host, optarg);
			case _O_PORT:				OPT_NUMBER("--port", server->port, 1, 65535, 0);
			case _O_UNIX:				OPT_SET(server->unix_path, optarg);
			case _O_UNIX_RM:			OPT_SET(server->unix_rm, true);
			case _O_UNIX_MODE:			OPT_NUMBER("--unix-mode", server->unix_mode, INT_MIN, INT_MAX, 8);
			case _O_USER:				OPT_SET(server->user, optarg);
			case _O_PASSWD:				OPT_SET(server->passwd, optarg);
			case _O_STATIC:				OPT_SET(server->static_path, optarg);
			case _O_BLANK:				OPT_SET(server->blank_path, optarg);
			case _O_DROP_SAME_FRAMES:	OPT_NUMBER("--drop-same-frames", server->drop_same_frames, 0, VIDEO_MAX_FPS, 0);
			case _O_SLOWDOWN:			OPT_SET(server->slowdown, true);
			case _O_FAKE_RESOLUTION:	OPT_RESOLUTION("--fake-resolution", server->fake_width, server->fake_height, false);
			case _O_FAKE_WIDTH:			OPT_RESOLUTION_OBSOLETE("--fake-width", "--fake-resolution", server->fake_width, 0, UINT_MAX);
			case _O_FAKE_HEIGHT:		OPT_RESOLUTION_OBSOLETE("--fake-height", "--fake-resolution", server->fake_height, 0, UINT_MAX);
			case _O_SERVER_TIMEOUT:		OPT_NUMBER("--server-timeout", server->timeout, 1, 60, 0);

#			ifdef WITH_GPIO
			case _O_GPIO_PROG_RUNNING:		OPT_NUMBER("--gpio-prog-running", gpio_pin_prog_running, 0, 256, 0);
			case _O_GPIO_STREAM_ONLINE:		OPT_NUMBER("--gpio-stream-online", gpio_pin_stream_online, 0, 256, 0);
			case _O_GPIO_HAS_HTTP_CLIENTS:	OPT_NUMBER("--gpio-has-http-clients", gpio_pin_has_http_clients, 0, 256, 0);
			case _O_GPIO_WORKERS_BUSY_AT:	OPT_NUMBER("--gpio-workers-busy-at", gpio_pin_workers_busy_at, 0, 256, 0);
#			endif

			case _O_LOG_LEVEL:		OPT_NUMBER("--log-level", log_level, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, 0);
			case _O_PERF:			OPT_SET(log_level, LOG_LEVEL_PERF);
			case _O_VERBOSE:		OPT_SET(log_level, LOG_LEVEL_VERBOSE);
			case _O_DEBUG:			OPT_SET(log_level, LOG_LEVEL_DEBUG);
			case _O_FORCE_COLORS:	OPT_SET(log_colored, true);
			case _O_NO_COLORS:		OPT_SET(log_colored, false);

			case _O_HELP:		_help(dev, encoder, server); return 1;
			case _O_VERSION:	_version(true); return 1;

			case 0:		break;
			default:	_help(dev, encoder, server); return -1;
		}
	}

#	undef OPT_CTL_AUTO
#	undef OPT_CTL
#	undef OPT_PARSE
#	ifdef WITH_OMX
#		undef OPT_GLITCHED_RESOLUTIONS
#	endif
#	undef OPT_RESOLUTION_OBSOLETE
#	undef OPT_RESOLUTION
#	undef OPT_NUMBER
#	undef OPT_SET
	return 0;
}

static int _parse_resolution(const char *str, unsigned *width, unsigned *height, bool limited) {
	unsigned tmp_width;
	unsigned tmp_height;

	if (sscanf(str, "%ux%u", &tmp_width, &tmp_height) != 2) {
		return -1;
	}
	if (limited) {
		if (tmp_width < VIDEO_MIN_WIDTH || tmp_width > VIDEO_MAX_WIDTH) {
			return -2;
		}
		if (tmp_height < VIDEO_MIN_HEIGHT || tmp_height > VIDEO_MAX_HEIGHT) {
			return -3;
		}
	}
	*width = tmp_width;
	*height = tmp_height;
	return 0;
}

#ifdef WITH_OMX
static int _parse_glitched_resolutions(const char *str, struct encoder_t *encoder) {
	char *str_copy;
	char *ptr;
	unsigned count = 0;
	unsigned width;
	unsigned height;

	assert((str_copy = strdup(str)) != NULL);

	ptr = strtok(str_copy, ",;:\n\t ");
	while (ptr != NULL) {
		if (count >= MAX_GLITCHED_RESOLUTIONS) {
			printf("Too big '--glitched-resolutions' list: maxlen=%u\n", MAX_GLITCHED_RESOLUTIONS);
			goto error;
		}

		switch (_parse_resolution(ptr, &width, &height, true)) {
			case -1:
				printf("Invalid resolution format of '%s' in '--glitched-resolutions=%s\n", ptr, str_copy);
				goto error;
			case -2:
				printf("Invalid width of '%s' in '--glitched-resolutions=%s: min=%u, max=%u\n",
					ptr, str_copy, VIDEO_MIN_WIDTH, VIDEO_MIN_HEIGHT);
				goto error;
			case -3:
				printf("Invalid width of '%s' in '--glitched-resolutions=%s: min=%u, max=%u\n",
					ptr, str_copy, VIDEO_MIN_WIDTH, VIDEO_MIN_HEIGHT);
				goto error;
			case 0: break;
			default: assert(0 && "Unknown error");
		}

		encoder->glitched_resolutions[count][0] = width;
		encoder->glitched_resolutions[count][1] = height;
		count += 1;

		ptr = strtok(NULL, ",;:\n\t ");
	}

	encoder->n_glitched_resolutions = count;
	free(str_copy);
	return 0;

	error:
		free(str_copy);
		return -1;
}
#endif

static void _version(bool nl) {
	printf(VERSION);
#	ifdef WITH_OMX
	printf(" + OMX");
#	endif
#	ifdef WITH_GPIO
	printf(" + GPIO");
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
	printf("    -d|--device </dev/path>  ───────────── Path to V4L2 device. Default: %s.\n\n", dev->path);
	printf("    -i|--input <N>  ────────────────────── Input channel. Default: %u.\n\n", dev->input);
	printf("    -r|--resolution <WxH>  ─────────────── Initial image resolution. Default: %ux%u.\n\n", dev->width, dev->height);
	printf("    -m|--format <fmt>  ─────────────────── Image format.\n");
	printf("                                           Available: %s; default: YUYV.\n\n", FORMATS_STR);
	printf("    -a|--tv-standard <std>  ────────────── Force TV standard.\n");
	printf("                                           Available: %s; default: disabled.\n\n", STANDARDS_STR);
	printf("    -f|--desired-fps <N>  ──────────────── Desired FPS. Default: maximum possible.\n\n");
	printf("    -z|--min-frame-size <N>  ───────────── Drop frames smaller then this limit. Useful if the device\n");
	printf("                                           produces small-sized garbage frames. Default: disabled.\n\n");
	printf("    -n|--persistent  ───────────────────── Don't re-initialize device on timeout. Default: disabled.\n\n");
	printf("    -t|--dv-timings  ───────────────────── Enable DV timings querying and events processing\n");
	printf("                                           to automatic resolution change. Default: disabled.\n\n");
	printf("    -b|--buffers <N>  ──────────────────── The number of buffers to receive data from the device.\n");
	printf("                                           Each buffer may processed using an independent thread.\n");
	printf("                                           Default: %u (the number of CPU cores (but not more than 4) + 1).\n\n", dev->n_buffers);
	printf("    -w|--workers <N>  ──────────────────── The number of worker threads but not more than buffers.\n");
	printf("                                           Default: %u (the number of CPU cores (but not more than 4)).\n\n", dev->n_workers);
	printf("    -q|--quality <N>  ──────────────────── Set quality of JPEG encoding from 1 to 100 (best). Default: %u.\n\n", encoder->quality);
	printf("    -c|--encoder <type>  ───────────────── Use specified encoder. It may affect the number of workers.\n\n");
#	ifdef WITH_OMX
	printf("    -g|--glitched-resolutions <WxH,...>  ─ Comma-separated list of resolutions that require forced\n");
#	endif
	printf("                                           encoding on CPU instead of OMX. Default: disabled.\n");
	printf("                                           Available: %s; default: CPU.\n\n", ENCODER_TYPES_STR);
	printf("    --device-timeout <seconds>  ────────── Timeout for device querying. Default: %u.\n\n", dev->timeout);
	printf("    --device-error-delay <seconds>  ────── Delay before trying to connect to the device again\n");
	printf("                                           after an error (timeout for example). Default: %u.\n\n", dev->error_delay);
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
	printf("Logging options:\n");
	printf("════════════════\n");
	printf("    --log-level <N>  ─ Verbosity level of messages from 0 (info) to 3 (debug).\n");
	printf("                       Enabling debugging messages can slow down the program.\n");
	printf("                       Available levels: 0 (info), 1 (performance), 2 (verbose), 3 (debug).\n");
	printf("                       Default: %u.\n\n", log_level);
	printf("    --perf  ────────── Enable performance messages (same as --log-level=1). Default: disabled.\n\n");
	printf("    --verbose  ─────── Enable verbose messages and lower (same as --log-level=2). Default: disabled.\n\n");
	printf("    --debug  ───────── Enable debug messages and lower (same as --log-level=3). Default: disabled.\n\n");
	printf("    --force-colors  ── Force color logging. Default: colored if stdout is a TTY.\n\n");
	printf("    --no-colors  ───── Disable color logging. Default: ditto.\n\n");
	printf("Help options:\n");
	printf("═════════════\n");
	printf("    -h|--help  ─────── Print this text and exit.\n\n");
	printf("    -v|--version  ──── Print version and exit.\n\n");
}
