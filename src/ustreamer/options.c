/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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


enum _OPT_VALUES {
	_O_DEVICE = 'd',
	_O_INPUT = 'i',
	_O_RESOLUTION = 'r',
	_O_FORMAT = 'm',
	_O_TV_STANDARD = 'a',
	_O_IO_METHOD = 'I',
	_O_DESIRED_FPS = 'f',
	_O_MIN_FRAME_SIZE = 'z',
	_O_PERSISTENT = 'n',
	_O_DV_TIMINGS = 't',
	_O_BUFFERS = 'b',
	_O_WORKERS = 'w',
	_O_QUALITY = 'q',
	_O_ENCODER = 'c',
#	ifdef WITH_OMX
	_O_GLITCHED_RESOLUTIONS = 'g',
#	endif
	_O_BLANK = 'k',
	_O_LAST_AS_BLANK = 'K',
	_O_SLOWDOWN = 'l',

	_O_HOST = 's',
	_O_PORT = 'p',
	_O_UNIX = 'U',
	_O_UNIX_RM = 'D',
	_O_UNIX_MODE = 'M',
#	ifdef WITH_SYSTEMD
	_O_SYSTEMD = 'S',
#	endif
	_O_DROP_SAME_FRAMES = 'e',
	_O_FAKE_RESOLUTION = 'R',

	_O_HELP = 'h',
	_O_VERSION = 'v',

	// Longs only

	_O_DEVICE_TIMEOUT = 10000,
	_O_DEVICE_ERROR_DELAY,

	_O_IMAGE_DEFAULT,
	_O_BRIGHTNESS,
	_O_CONTRAST,
	_O_SATURATION,
	_O_HUE,
	_O_GAMMA,
	_O_SHARPNESS,
	_O_BACKLIGHT_COMPENSATION,
	_O_WHITE_BALANCE,
	_O_GAIN,
	_O_COLOR_EFFECT,
	_O_ROTATE,
	_O_FLIP_VERTICAL,
	_O_FLIP_HORIZONTAL,

	_O_USER,
	_O_PASSWD,
	_O_STATIC,
	_O_ALLOW_ORIGIN,
	_O_TCP_NODELAY,
	_O_SERVER_TIMEOUT,

#	define ADD_SINK(_prefix) \
		_O_##_prefix, \
		_O_##_prefix##_MODE, \
		_O_##_prefix##_RM, \
		_O_##_prefix##_CLIENT_TTL, \
		_O_##_prefix##_TIMEOUT,
	ADD_SINK(SINK)
	ADD_SINK(RAW_SINK)
#	ifdef WITH_OMX
	ADD_SINK(H264_SINK)
	_O_H264_BITRATE,
	_O_H264_GOP,
#	endif
#	undef ADD_SINK

#	ifdef WITH_GPIO
	_O_GPIO_DEVICE,
	_O_GPIO_CONSUMER_PREFIX,
	_O_GPIO_PROG_RUNNING,
	_O_GPIO_STREAM_ONLINE,
	_O_GPIO_HAS_HTTP_CLIENTS,
#	endif

#	ifdef HAS_PDEATHSIG
	_O_EXIT_ON_PARENT_DEATH,
#	endif
	_O_EXIT_ON_NO_CLIENTS,
#	ifdef WITH_SETPROCTITLE
	_O_PROCESS_NAME_PREFIX,
#	endif
	_O_NOTIFY_PARENT,

	_O_LOG_LEVEL,
	_O_PERF,
	_O_VERBOSE,
	_O_DEBUG,
	_O_FORCE_LOG_COLORS,
	_O_NO_LOG_COLORS,

	_O_FEATURES,
};

static const struct option _LONG_OPTS[] = {
	{"device",					required_argument,	NULL,	_O_DEVICE},
	{"input",					required_argument,	NULL,	_O_INPUT},
	{"resolution",				required_argument,	NULL,	_O_RESOLUTION},
	{"format",					required_argument,	NULL,	_O_FORMAT},
	{"tv-standard",				required_argument,	NULL,	_O_TV_STANDARD},
	{"io-method",				required_argument,	NULL,	_O_IO_METHOD},
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
	{"blank",					required_argument,	NULL,	_O_BLANK},
	{"last-as-blank",			required_argument,	NULL,	_O_LAST_AS_BLANK},
	{"slowdown",				no_argument,		NULL,	_O_SLOWDOWN},
	{"device-timeout",			required_argument,	NULL,	_O_DEVICE_TIMEOUT},
	{"device-error-delay",		required_argument,	NULL,	_O_DEVICE_ERROR_DELAY},

	{"image-default",			no_argument,		NULL,	_O_IMAGE_DEFAULT},
	{"brightness",				required_argument,	NULL,	_O_BRIGHTNESS},
	{"contrast",				required_argument,	NULL,	_O_CONTRAST},
	{"saturation",				required_argument,	NULL,	_O_SATURATION},
	{"hue",						required_argument,	NULL,	_O_HUE},
	{"gamma",					required_argument,	NULL,	_O_GAMMA},
	{"sharpness",				required_argument,	NULL,	_O_SHARPNESS},
	{"backlight-compensation",	required_argument,	NULL,	_O_BACKLIGHT_COMPENSATION},
	{"white-balance",			required_argument,	NULL,	_O_WHITE_BALANCE},
	{"gain",					required_argument,	NULL,	_O_GAIN},
	{"color-effect",			required_argument,	NULL,	_O_COLOR_EFFECT},
	{"rotate",				required_argument,	NULL,	_O_ROTATE},
	{"flip-vertical",			required_argument,	NULL,	_O_FLIP_VERTICAL},
	{"flip-horizontal",			required_argument,	NULL,	_O_FLIP_HORIZONTAL},

	{"host",					required_argument,	NULL,	_O_HOST},
	{"port",					required_argument,	NULL,	_O_PORT},
	{"unix",					required_argument,	NULL,	_O_UNIX},
	{"unix-rm",					no_argument,		NULL,	_O_UNIX_RM},
	{"unix-mode",				required_argument,	NULL,	_O_UNIX_MODE},
#	ifdef WITH_SYSTEMD
	{"systemd",					no_argument,		NULL,	_O_SYSTEMD},
#	endif
	{"user",					required_argument,	NULL,	_O_USER},
	{"passwd",					required_argument,	NULL,	_O_PASSWD},
	{"static",					required_argument,	NULL,	_O_STATIC},
	{"drop-same-frames",		required_argument,	NULL,	_O_DROP_SAME_FRAMES},
	{"allow-origin",			required_argument,	NULL,	_O_ALLOW_ORIGIN},
	{"fake-resolution",			required_argument,	NULL,	_O_FAKE_RESOLUTION},
	{"tcp-nodelay",				no_argument,		NULL,	_O_TCP_NODELAY},
	{"server-timeout",			required_argument,	NULL,	_O_SERVER_TIMEOUT},

#	define ADD_SINK(_opt, _prefix) \
		{_opt "sink",				required_argument,	NULL,	_O_##_prefix}, \
		{_opt "sink-mode",			required_argument,	NULL,	_O_##_prefix##_MODE}, \
		{_opt "sink-rm",			no_argument,		NULL,	_O_##_prefix##_RM}, \
		{_opt "sink-client-ttl",	required_argument,	NULL,	_O_##_prefix##_CLIENT_TTL}, \
		{_opt "sink-timeout",		required_argument,	NULL,	_O_##_prefix##_TIMEOUT},
	ADD_SINK("", SINK)
	ADD_SINK("raw-", RAW_SINK)
#	ifdef WITH_OMX
	ADD_SINK("h264-", H264_SINK)
	{"h264-bitrate",			required_argument,	NULL,	_O_H264_BITRATE},
	{"h264-gop",				required_argument,	NULL,	_O_H264_GOP},
#	endif
#	undef ADD_SINK

#	ifdef WITH_GPIO
	{"gpio-device",				required_argument,	NULL,	_O_GPIO_DEVICE},
	{"gpio-consumer-prefix",	required_argument,	NULL,	_O_GPIO_CONSUMER_PREFIX},
	{"gpio-prog-running",		required_argument,	NULL,	_O_GPIO_PROG_RUNNING},
	{"gpio-stream-online",		required_argument,	NULL,	_O_GPIO_STREAM_ONLINE},
	{"gpio-has-http-clients",	required_argument,	NULL,	_O_GPIO_HAS_HTTP_CLIENTS},
#	endif

#	ifdef HAS_PDEATHSIG
	{"exit-on-parent-death",	no_argument,		NULL,	_O_EXIT_ON_PARENT_DEATH},
#	endif
	{"exit-on-no-clients",		required_argument,	NULL,	_O_EXIT_ON_NO_CLIENTS},
#	ifdef WITH_SETPROCTITLE
	{"process-name-prefix",		required_argument,	NULL,	_O_PROCESS_NAME_PREFIX},
#	endif
	{"notify-parent",			no_argument,		NULL,	_O_NOTIFY_PARENT},

	{"log-level",				required_argument,	NULL,	_O_LOG_LEVEL},
	{"perf",					no_argument,		NULL,	_O_PERF},
	{"verbose",					no_argument,		NULL,	_O_VERBOSE},
	{"debug",					no_argument,		NULL,	_O_DEBUG},
	{"force-log-colors",		no_argument,		NULL,	_O_FORCE_LOG_COLORS},
	{"no-log-colors",			no_argument,		NULL,	_O_NO_LOG_COLORS},

	{"help",					no_argument,		NULL,	_O_HELP},
	{"version",					no_argument,		NULL,	_O_VERSION},
	{"features",				no_argument,		NULL,	_O_FEATURES},

	{NULL, 0, NULL, 0},
};


static int _parse_resolution(const char *str, unsigned *width, unsigned *height, bool limited);

static void _features(void);
static void _help(FILE *fp, device_s *dev, encoder_s *enc, stream_s *stream, server_s *server);


options_s *options_init(unsigned argc, char *argv[]) {
	options_s *options;
	A_CALLOC(options, 1);
	options->argc = argc;
	options->argv = argv;

	A_CALLOC(options->argv_copy, argc);
	for (unsigned index = 0; index < argc; ++index) {
		assert(options->argv_copy[index] = strdup(argv[index]));
	}
	return options;
}

void options_destroy(options_s *options) {
#	define ADD_SINK(_prefix) { \
			if (options->_prefix) { \
				memsink_destroy(options->_prefix); \
			} \
		}
	ADD_SINK(sink);
	ADD_SINK(raw_sink);
#	ifdef WITH_OMX
	ADD_SINK(h264_sink);
#	endif
#	undef ADD_SINK

	if (options->blank) {
		frame_destroy(options->blank);
	}

	for (unsigned index = 0; index < options->argc; ++index) {
		free(options->argv_copy[index]);
	}
	free(options->argv_copy);

	free(options);
}


int options_parse(options_s *options, device_s *dev, encoder_s *enc, stream_s *stream, server_s *server) {
#	define OPT_SET(_dest, _value) { \
			_dest = _value; \
			break; \
		}

#	define OPT_NUMBER(_name, _dest, _min, _max, _base) { \
			errno = 0; char *_end = NULL; long long _tmp = strtoll(optarg, &_end, _base); \
			if (errno || *_end || _tmp < _min || _tmp > _max) { \
				printf("Invalid value for '%s=%s': min=%lld, max=%lld\n", _name, optarg, (long long)_min, (long long)_max); \
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

#	define OPT_PARSE(_name, _dest, _func, _invalid, _available) { \
			if ((_dest = _func(optarg)) == _invalid) { \
				printf("Unknown " _name ": %s; available: %s\n", optarg, _available); \
				return -1; \
			} \
			break; \
		}

#	define OPT_CTL_DEFAULT_NOBREAK(_dest) { \
			dev->ctl._dest.mode = CTL_MODE_DEFAULT; \
		}

#	define OPT_CTL_MANUAL(_dest) { \
			if (!strcasecmp(optarg, "default")) { \
				OPT_CTL_DEFAULT_NOBREAK(_dest); \
			} else { \
				dev->ctl._dest.mode = CTL_MODE_VALUE; \
				OPT_NUMBER("--"#_dest, dev->ctl._dest.value, INT_MIN, INT_MAX, 0); \
			} \
			break; \
		}

#	define OPT_CTL_AUTO(_dest) { \
			if (!strcasecmp(optarg, "default")) { \
				OPT_CTL_DEFAULT_NOBREAK(_dest); \
			} else if (!strcasecmp(optarg, "auto")) { \
				dev->ctl._dest.mode = CTL_MODE_AUTO; \
			} else { \
				dev->ctl._dest.mode = CTL_MODE_VALUE; \
				OPT_NUMBER("--"#_dest, dev->ctl._dest.value, INT_MIN, INT_MAX, 0); \
			} \
			break; \
		}

	char *blank_path = NULL;

#	define ADD_SINK(_prefix) \
		char *_prefix##_name = NULL; \
		mode_t _prefix##_mode = 0660; \
		bool _prefix##_rm = false; \
		unsigned _prefix##_client_ttl = 10; \
		unsigned _prefix##_timeout = 1;
	ADD_SINK(sink);
	ADD_SINK(raw_sink);
#	ifdef WITH_OMX
	ADD_SINK(h264_sink);
#	endif
#	undef ADD_SINK

#	ifdef WITH_SETPROCTITLE
	char *process_name_prefix = NULL;
#	endif

	char short_opts[128];
	build_short_options(_LONG_OPTS, short_opts, 128);

	for (int ch; (ch = getopt_long(options->argc, options->argv_copy, short_opts, _LONG_OPTS, NULL)) >= 0;) {
		switch (ch) {
			case _O_DEVICE:			OPT_SET(dev->path, optarg);
			case _O_INPUT:			OPT_NUMBER("--input", dev->input, 0, 128, 0);
			case _O_RESOLUTION:		OPT_RESOLUTION("--resolution", dev->width, dev->height, true);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case _O_FORMAT:			OPT_PARSE("pixel format", dev->format, device_parse_format, FORMAT_UNKNOWN, FORMATS_STR);
#			pragma GCC diagnostic pop
			case _O_TV_STANDARD:	OPT_PARSE("TV standard", dev->standard, device_parse_standard, STANDARD_UNKNOWN, STANDARDS_STR);
			case _O_IO_METHOD:		OPT_PARSE("IO method", dev->io_method, device_parse_io_method, IO_METHOD_UNKNOWN, IO_METHODS_STR);
			case _O_DESIRED_FPS:	OPT_NUMBER("--desired-fps", dev->desired_fps, 0, VIDEO_MAX_FPS, 0);
			case _O_MIN_FRAME_SIZE:	OPT_NUMBER("--min-frame-size", dev->min_frame_size, 1, 8192, 0);
			case _O_PERSISTENT:		OPT_SET(dev->persistent, true);
			case _O_DV_TIMINGS:		OPT_SET(dev->dv_timings, true);
			case _O_BUFFERS:		OPT_NUMBER("--buffers", dev->n_bufs, 1, 32, 0);
			case _O_WORKERS:		OPT_NUMBER("--workers", enc->n_workers, 1, 32, 0);
			case _O_QUALITY:		OPT_NUMBER("--quality", dev->jpeg_quality, 1, 100, 0);
			case _O_ENCODER:		OPT_PARSE("encoder type", enc->type, encoder_parse_type, ENCODER_TYPE_UNKNOWN, ENCODER_TYPES_STR);
#			ifdef WITH_OMX
			case _O_GLITCHED_RESOLUTIONS: break;
#			endif
			case _O_BLANK:				OPT_SET(blank_path, optarg);
			case _O_LAST_AS_BLANK:		OPT_NUMBER("--last-as-blank", stream->last_as_blank, 0, 86400, 0);
			case _O_SLOWDOWN:			OPT_SET(stream->slowdown, true);
			case _O_DEVICE_TIMEOUT:		OPT_NUMBER("--device-timeout", dev->timeout, 1, 60, 0);
			case _O_DEVICE_ERROR_DELAY:	OPT_NUMBER("--device-error-delay", stream->error_delay, 1, 60, 0);

			case _O_IMAGE_DEFAULT:
				OPT_CTL_DEFAULT_NOBREAK(brightness);
				OPT_CTL_DEFAULT_NOBREAK(contrast);
				OPT_CTL_DEFAULT_NOBREAK(saturation);
				OPT_CTL_DEFAULT_NOBREAK(hue);
				OPT_CTL_DEFAULT_NOBREAK(gamma);
				OPT_CTL_DEFAULT_NOBREAK(sharpness);
				OPT_CTL_DEFAULT_NOBREAK(backlight_compensation);
				OPT_CTL_DEFAULT_NOBREAK(white_balance);
				OPT_CTL_DEFAULT_NOBREAK(gain);
				OPT_CTL_DEFAULT_NOBREAK(color_effect);
				OPT_CTL_DEFAULT_NOBREAK(rotate);
				OPT_CTL_DEFAULT_NOBREAK(flip_vertical);
				OPT_CTL_DEFAULT_NOBREAK(flip_horizontal);
				break;
			case _O_BRIGHTNESS:				OPT_CTL_AUTO(brightness);
			case _O_CONTRAST:				OPT_CTL_MANUAL(contrast);
			case _O_SATURATION:				OPT_CTL_MANUAL(saturation);
			case _O_HUE:					OPT_CTL_AUTO(hue);
			case _O_GAMMA:					OPT_CTL_MANUAL(gamma);
			case _O_SHARPNESS:				OPT_CTL_MANUAL(sharpness);
			case _O_BACKLIGHT_COMPENSATION:	OPT_CTL_MANUAL(backlight_compensation);
			case _O_WHITE_BALANCE:			OPT_CTL_AUTO(white_balance);
			case _O_GAIN:					OPT_CTL_AUTO(gain);
			case _O_COLOR_EFFECT:			OPT_CTL_MANUAL(color_effect);
			case _O_ROTATE:			 	OPT_CTL_MANUAL(rotate);
			case _O_FLIP_VERTICAL:			OPT_CTL_MANUAL(flip_vertical);
			case _O_FLIP_HORIZONTAL:		OPT_CTL_MANUAL(flip_horizontal);

			case _O_HOST:				OPT_SET(server->host, optarg);
			case _O_PORT:				OPT_NUMBER("--port", server->port, 1, 65535, 0);
			case _O_UNIX:				OPT_SET(server->unix_path, optarg);
			case _O_UNIX_RM:			OPT_SET(server->unix_rm, true);
			case _O_UNIX_MODE:			OPT_NUMBER("--unix-mode", server->unix_mode, INT_MIN, INT_MAX, 8);
#			ifdef WITH_SYSTEMD
			case _O_SYSTEMD:			OPT_SET(server->systemd, true);
#			endif
			case _O_USER:				OPT_SET(server->user, optarg);
			case _O_PASSWD:				OPT_SET(server->passwd, optarg);
			case _O_STATIC:				OPT_SET(server->static_path, optarg);
			case _O_DROP_SAME_FRAMES:	OPT_NUMBER("--drop-same-frames", server->drop_same_frames, 0, VIDEO_MAX_FPS, 0);
			case _O_FAKE_RESOLUTION:	OPT_RESOLUTION("--fake-resolution", server->fake_width, server->fake_height, false);
			case _O_ALLOW_ORIGIN:		OPT_SET(server->allow_origin, optarg);
			case _O_TCP_NODELAY:		OPT_SET(server->tcp_nodelay, true);
			case _O_SERVER_TIMEOUT:		OPT_NUMBER("--server-timeout", server->timeout, 1, 60, 0);

#			define ADD_SINK(_opt, _lp, _up) \
				case _O_##_up:				OPT_SET(_lp##_name, optarg); \
				case _O_##_up##_MODE:		OPT_NUMBER("--" #_opt "sink-mode", _lp##_mode, INT_MIN, INT_MAX, 8); \
				case _O_##_up##_RM:			OPT_SET(_lp##_rm, true); \
				case _O_##_up##_CLIENT_TTL:	OPT_NUMBER("--" #_opt "sink-client-ttl", _lp##_client_ttl, 1, 60, 0); \
				case _O_##_up##_TIMEOUT:	OPT_NUMBER("--" #_opt "sink-timeout", _lp##_timeout, 1, 60, 0);
			ADD_SINK("", sink, SINK)
			ADD_SINK("raw-", raw_sink, RAW_SINK)
#			ifdef WITH_OMX
			ADD_SINK("h264-", h264_sink, H264_SINK)
			case _O_H264_BITRATE:	OPT_NUMBER("--h264-bitrate", stream->h264_bitrate, 25, 25000, 0);
			case _O_H264_GOP:		OPT_NUMBER("--h264-gop", stream->h264_gop, 0, 60, 0);
#			endif
#			undef ADD_SINK

#			ifdef WITH_GPIO
			case _O_GPIO_DEVICE:			OPT_SET(us_gpio.path, optarg);
			case _O_GPIO_CONSUMER_PREFIX:	OPT_SET(us_gpio.consumer_prefix, optarg);
			case _O_GPIO_PROG_RUNNING:		OPT_NUMBER("--gpio-prog-running", us_gpio.prog_running.pin, 0, 256, 0);
			case _O_GPIO_STREAM_ONLINE:		OPT_NUMBER("--gpio-stream-online", us_gpio.stream_online.pin, 0, 256, 0);
			case _O_GPIO_HAS_HTTP_CLIENTS:	OPT_NUMBER("--gpio-has-http-clients", us_gpio.has_http_clients.pin, 0, 256, 0);
#			endif

#			ifdef HAS_PDEATHSIG
			case _O_EXIT_ON_PARENT_DEATH:
				if (process_track_parent_death() < 0) {
					return -1;
				};
				break;
#			endif
			case _O_EXIT_ON_NO_CLIENTS:		OPT_NUMBER("--exit-on-no-clients", server->exit_on_no_clients, 0, 86400, 0);
#			ifdef WITH_SETPROCTITLE
			case _O_PROCESS_NAME_PREFIX:	OPT_SET(process_name_prefix, optarg);
#			endif
			case _O_NOTIFY_PARENT:			OPT_SET(server->notify_parent, true);

			case _O_LOG_LEVEL:			OPT_NUMBER("--log-level", us_log_level, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, 0);
			case _O_PERF:				OPT_SET(us_log_level, LOG_LEVEL_PERF);
			case _O_VERBOSE:			OPT_SET(us_log_level, LOG_LEVEL_VERBOSE);
			case _O_DEBUG:				OPT_SET(us_log_level, LOG_LEVEL_DEBUG);
			case _O_FORCE_LOG_COLORS:	OPT_SET(us_log_colored, true);
			case _O_NO_LOG_COLORS:		OPT_SET(us_log_colored, false);

			case _O_HELP:		_help(stdout, dev, enc, stream, server); return 1;
			case _O_VERSION:	puts(VERSION); return 1;
			case _O_FEATURES:	_features(); return 1;

			case 0:		break;
			default:	return -1;
		}
	}

	options->blank = blank_frame_init(blank_path);
	stream->blank = options->blank;

#	define ADD_SINK(_label, _prefix) { \
			if (_prefix##_name && _prefix##_name[0] != '\0') { \
				options->_prefix = memsink_init( \
					_label, \
					_prefix##_name, \
					true, \
					_prefix##_mode, \
					_prefix##_rm, \
					_prefix##_client_ttl, \
					_prefix##_timeout \
				); \
			} \
			stream->_prefix = options->_prefix; \
		}
	ADD_SINK("JPEG", sink);
	ADD_SINK("RAW", raw_sink);
#	ifdef WITH_OMX
	ADD_SINK("H264", h264_sink);
#	endif
#	undef ADD_SINK

#	ifdef WITH_SETPROCTITLE
	if (process_name_prefix != NULL) {
		process_set_name_prefix(options->argc, options->argv, process_name_prefix);
	}
#	endif

#	undef OPT_CTL_AUTO
#	undef OPT_CTL_MANUAL
#	undef OPT_CTL_DEFAULT_NOBREAK
#	undef OPT_PARSE
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

static void _features(void) {
#	ifdef WITH_OMX
	puts("+ WITH_OMX");
#	else
	puts("- WITH_OMX");
#	endif

#	ifdef WITH_GPIO
	puts("+ WITH_GPIO");
#	else
	puts("- WITH_GPIO");
#	endif

#	ifdef WITH_SYSTEMD
	puts("+ WITH_SYSTEMD");
#	else
	puts("- WITH_SYSTEMD");
#	endif

#	ifdef WITH_PTHREAD_NP
	puts("+ WITH_PTHREAD_NP");
#	else
	puts("- WITH_PTHREAD_NP");
#	endif

#	ifdef WITH_SETPROCTITLE
	puts("+ WITH_SETPROCTITLE");
#	else
	puts("- WITH_SETPROCTITLE");
#	endif

#	ifdef HAS_PDEATHSIG
	puts("+ HAS_PDEATHSIG");
#	else
	puts("- HAS_PDEATHSIG");
#	endif
}

static void _help(FILE *fp, device_s *dev, encoder_s *enc, stream_s *stream, server_s *server) {
#	define SAY(_msg, ...) fprintf(fp, _msg "\n", ##__VA_ARGS__)
	SAY("\nuStreamer - Lightweight and fast MJPG-HTTP streamer");
	SAY("═══════════════════════════════════════════════════");
	SAY("Version: %s; license: GPLv3", VERSION);
	SAY("Copyright (C) 2018-2021 Maxim Devaev <mdevaev@gmail.com>\n");
	SAY("Capturing options:");
	SAY("══════════════════");
	SAY("    -d|--device </dev/path>  ───────────── Path to V4L2 device. Default: %s.\n", dev->path);
	SAY("    -i|--input <N>  ────────────────────── Input channel. Default: %u.\n", dev->input);
	SAY("    -r|--resolution <WxH>  ─────────────── Initial image resolution. Default: %ux%u.\n", dev->width, dev->height);
	SAY("    -m|--format <fmt>  ─────────────────── Image format.");
	SAY("                                           Available: %s; default: YUYV.\n", FORMATS_STR);
	SAY("    -a|--tv-standard <std>  ────────────── Force TV standard.");
	SAY("                                           Available: %s; default: disabled.\n", STANDARDS_STR);
	SAY("    -I|--io-method <method>  ───────────── Set V4L2 IO method (see kernel documentation).");
	SAY("                                           Changing of this parameter may increase the performance. Or not.");
	SAY("                                           Available: %s; default: MMAP.\n", IO_METHODS_STR);
	SAY("    -f|--desired-fps <N>  ──────────────── Desired FPS. Default: maximum possible.\n");
	SAY("    -z|--min-frame-size <N>  ───────────── Drop frames smaller then this limit. Useful if the device");
	SAY("                                           produces small-sized garbage frames. Default: %zu bytes.\n", dev->min_frame_size);
	SAY("    -n|--persistent  ───────────────────── Don't re-initialize device on timeout. Default: disabled.\n");
	SAY("    -t|--dv-timings  ───────────────────── Enable DV timings querying and events processing");
	SAY("                                           to automatic resolution change. Default: disabled.\n");
	SAY("    -b|--buffers <N>  ──────────────────── The number of buffers to receive data from the device.");
	SAY("                                           Each buffer may processed using an independent thread.");
	SAY("                                           Default: %u (the number of CPU cores (but not more than 4) + 1).\n", dev->n_bufs);
	SAY("    -w|--workers <N>  ──────────────────── The number of worker threads but not more than buffers.");
	SAY("                                           Default: %u (the number of CPU cores (but not more than 4)).\n", enc->n_workers);
	SAY("    -q|--quality <N>  ──────────────────── Set quality of JPEG encoding from 1 to 100 (best). Default: %u.", dev->jpeg_quality);
	SAY("                                           Note: If HW encoding is used (JPEG source format selected),");
	SAY("                                           this parameter attempts to configure the camera");
	SAY("                                           or capture device hardware's internal encoder.");
	SAY("                                           It does not re-encode MJPG to MJPG to change the quality level");
	SAY("                                           for sources that already output MJPG.\n");
	SAY("    -c|--encoder <type>  ───────────────── Use specified encoder. It may affect the number of workers.");
	SAY("                                           Available:");
	SAY("                                             * CPU  ── Software MJPG encoding (default);");
#	ifdef WITH_OMX
	SAY("                                             * OMX  ── GPU hardware accelerated MJPG encoding with OpenMax;");
#	endif
	SAY("                                             * HW  ─── Use pre-encoded MJPG frames directly from camera hardware.");
	SAY("                                             * NOOP  ─ Don't compress MJPG stream (do nothing).\n");
#	ifdef WITH_OMX
	SAY("    -g|--glitched-resolutions <WxH,...>  ─ It doesn't do anything. Still here for compatibility.\n");
#	endif
	SAY("    -k|--blank <path>  ─────────────────── Path to JPEG file that will be shown when the device is disconnected");
	SAY("                                           during the streaming. Default: black screen 640x480 with 'NO SIGNAL'.\n");
	SAY("    -K|--last-as-blank <sec>  ──────────── Show the last frame received from the camera after it was disconnected,");
	SAY("                                           but no more than specified time (or endlessly if 0 is specified).");
	SAY("                                           If the device has not yet been online, display 'NO SIGNAL' or the image");
	SAY("                                           specified by option --blank. Default: disabled.");
	SAY("                                           Note: currently this option has no effect on memory sinks.\n");
	SAY("    -l|--slowdown  ─────────────────────── Slowdown capturing to 1 FPS or less when no stream or sink clients");
	SAY("                                           are connected. Useful to reduce CPU consumption. Default: disabled.\n");
	SAY("    --device-timeout <sec>  ────────────── Timeout for device querying. Default: %u.\n", dev->timeout);
	SAY("    --device-error-delay <sec>  ────────── Delay before trying to connect to the device again");
	SAY("                                           after an error (timeout for example). Default: %u.\n", stream->error_delay);
	SAY("Image control options:");
	SAY("══════════════════════");
	SAY("    --image-default  ────────────────────── Reset all image settings below to default. Default: no change.\n");
	SAY("    --brightness <N|auto|default>  ──────── Set brightness. Default: no change.\n");
	SAY("    --contrast <N|default>  ─────────────── Set contrast. Default: no change.\n");
	SAY("    --saturation <N|default>  ───────────── Set saturation. Default: no change.\n");
	SAY("    --hue <N|auto|default>  ─────────────── Set hue. Default: no change.\n");
	SAY("    --gamma <N|default> ─────────────────── Set gamma. Default: no change.\n");
	SAY("    --sharpness <N|default>  ────────────── Set sharpness. Default: no change.\n");
	SAY("    --backlight-compensation <N|default>  ─ Set backlight compensation. Default: no change.\n");
	SAY("    --white-balance <N|auto|default>  ───── Set white balance. Default: no change.\n");
	SAY("    --gain <N|auto|default>  ────────────── Set gain. Default: no change.\n");
	SAY("    --color-effect <N|default>  ─────────── Set color effect. Default: no change.\n");
	SAY("    --rotate <N|default>  ───────────────── Set rotation. Default: no change.\n");
	SAY("    --flip-vertical <1|0|default>  ──────── Set vertical flip. Default: no change.\n");
	SAY("    --flip-horizontal <1|0|default>  ────── Set horizontal flip. Default: no change.\n");
	SAY("    Hint: use v4l2-ctl --list-ctrls-menus to query available controls of the device.\n");
	SAY("HTTP server options:");
	SAY("════════════════════");
	SAY("    -s|--host <address>  ──────── Listen on Hostname or IP. Default: %s.\n", server->host);
	SAY("    -p|--port <N>  ────────────── Bind to this TCP port. Default: %u.\n", server->port);
	SAY("    -U|--unix <path>  ─────────── Bind to UNIX domain socket. Default: disabled.\n");
	SAY("    -D|--unix-rm  ─────────────── Try to remove old UNIX socket file before binding. Default: disabled.\n");
	SAY("    -M|--unix-mode <mode>  ────── Set UNIX socket file permissions (like 777). Default: disabled.\n");
#	ifdef WITH_SYSTEMD
	SAY("    -S|--systemd  ─────────────── Bind to systemd socket for socket activation.\n");
#	endif
	SAY("    --user <name>  ────────────── HTTP basic auth user. Default: disabled.\n");
	SAY("    --passwd <str>  ───────────── HTTP basic auth passwd. Default: empty.\n");
	SAY("    --static <path> ───────────── Path to dir with static files instead of embedded root index page.");
	SAY("                                  Symlinks are not supported for security reasons. Default: disabled.\n");
	SAY("    -e|--drop-same-frames <N>  ── Don't send identical frames to clients, but no more than specified number.");
	SAY("                                  It can significantly reduce the outgoing traffic, but will increase");
	SAY("                                  the CPU loading. Don't use this option with analog signal sources");
	SAY("                                  or webcams, it's useless. Default: disabled.\n");
	SAY("    -R|--fake-resolution <WxH>  ─ Override image resolution for the /state. Default: disabled.\n");
	SAY("    --tcp-nodelay  ────────────── Set TCP_NODELAY flag to the client /stream socket. Only for TCP socket.");
	SAY("                                  Default: disabled.\n");
	SAY("    --allow-origin <str>  ─────── Set Access-Control-Allow-Origin header. Default: disabled.\n");
	SAY("    --server-timeout <sec>  ───── Timeout for client connections. Default: %u.\n", server->timeout);
#	define ADD_SINK(_name, _opt) \
		SAY(_name " sink options:"); \
		SAY("══════════════════"); \
		SAY("    --" _opt "sink <name>  ─────────── Use the shared memory to sink " _name " frames. Default: disabled.\n"); \
		SAY("    --" _opt "sink-mode <mode>  ────── Set " _name " sink permissions (like 777). Default: 660.\n"); \
		SAY("    --" _opt "sink-rm  ─────────────── Remove shared memory on stop. Default: disabled.\n"); \
		SAY("    --" _opt "sink-client-ttl <sec>  ─ Client TTL. Default: 10.\n"); \
		SAY("    --" _opt "sink-timeout <sec>  ──── Timeout for lock. Default: 1.\n");
	ADD_SINK("JPEG", "")
	ADD_SINK("RAW", "raw-")
#	ifdef WITH_OMX
	ADD_SINK("H264", "h264-")
	SAY("    --h264-bitrate <kbps>  ──────── H264 bitrate in Kbps. Default: %u.\n", stream->h264_bitrate);
	SAY("    --h264-gop <N>  ─────────────── Intarval between keyframes. Default: %u.\n", stream->h264_gop);
#	endif
#	undef ADD_SINK
#	ifdef WITH_GPIO
	SAY("GPIO options:");
	SAY("═════════════");
	SAY("    --gpio-device </dev/path>  ───── Path to GPIO character device. Default: %s.\n", us_gpio.path);
	SAY("    --gpio-consumer-prefix <str>  ── Consumer prefix for GPIO outputs. Default: %s.\n", us_gpio.consumer_prefix);
	SAY("    --gpio-prog-running <pin>  ───── Set 1 on GPIO pin while uStreamer is running. Default: disabled.\n");
	SAY("    --gpio-stream-online <pin>  ──── Set 1 while streaming. Default: disabled.\n");
	SAY("    --gpio-has-http-clients <pin>  ─ Set 1 while stream has at least one client. Default: disabled.\n");
#	endif
#	if (defined(HAS_PDEATHSIG) || defined(WITH_SETPROCTITLE))
	SAY("Process options:");
	SAY("════════════════");
#	endif
#	ifdef HAS_PDEATHSIG
	SAY("    --exit-on-parent-death  ─────── Exit the program if the parent process is dead. Default: disabled.\n");
#	endif
	SAY("    --exit-on-no-clients <sec> ──── Exit the program if there have been no stream or sink clients");
	SAY("                                    or any HTTP requests in the last N seconds. Default: 0 (disabled)\n");
#	ifdef WITH_SETPROCTITLE
	SAY("    --process-name-prefix <str>  ── Set process name prefix which will be displayed in the process list");
	SAY("                                    like '<str>: ustreamer --blah-blah-blah'. Default: disabled.\n");
	SAY("    --notify-parent  ────────────── Send SIGUSR2 to the parent process when the stream parameters are changed.");
	SAY("                                    Checking changes is performed for the online flag and image resolution.\n");
#	endif
	SAY("Logging options:");
	SAY("════════════════");
	SAY("    --log-level <N>  ──── Verbosity level of messages from 0 (info) to 3 (debug).");
	SAY("                          Enabling debugging messages can slow down the program.");
	SAY("                          Available levels: 0 (info), 1 (performance), 2 (verbose), 3 (debug).");
	SAY("                          Default: %d.\n", us_log_level);
	SAY("    --perf  ───────────── Enable performance messages (same as --log-level=1). Default: disabled.\n");
	SAY("    --verbose  ────────── Enable verbose messages and lower (same as --log-level=2). Default: disabled.\n");
	SAY("    --debug  ──────────── Enable debug messages and lower (same as --log-level=3). Default: disabled.\n");
	SAY("    --force-log-colors  ─ Force color logging. Default: colored if stderr is a TTY.\n");
	SAY("    --no-log-colors  ──── Disable color logging. Default: ditto.\n");
	SAY("Help options:");
	SAY("═════════════");
	SAY("    -h|--help  ─────── Print this text and exit.\n");
	SAY("    -v|--version  ──── Print version and exit.\n");
	SAY("    --features  ────── Print list of supported features.\n");
#	undef SAY
}
