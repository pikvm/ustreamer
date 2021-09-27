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


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <float.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include "../libs/config.h"
#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/options.h"

#include "file.h"


enum _OPT_VALUES {
	_O_SINK = 's',
	_O_SINK_TIMEOUT = 't',
	_O_OUTPUT = 'o',
	_O_OUTPUT_JSON = 'j',
	_O_COUNT = 'c',
	_O_INTERVAL = 'i',

	_O_HELP = 'h',
	_O_VERSION = 'v',

	_O_LOG_LEVEL = 10000,
	_O_PERF,
	_O_VERBOSE,
	_O_DEBUG,
	_O_FORCE_LOG_COLORS,
	_O_NO_LOG_COLORS,
};

static const struct option _LONG_OPTS[] = {
	{"sink",				required_argument,	NULL,	_O_SINK},
	{"sink-timeout",		required_argument,	NULL,	_O_SINK_TIMEOUT},
	{"output",				required_argument,	NULL,	_O_OUTPUT},
	{"output-json",			no_argument,		NULL,	_O_OUTPUT_JSON},
	{"count",				required_argument,	NULL,	_O_COUNT},
	{"interval",			required_argument,	NULL,	_O_INTERVAL},

	{"log-level",			required_argument,	NULL,	_O_LOG_LEVEL},
	{"perf",				no_argument,		NULL,	_O_PERF},
	{"verbose",				no_argument,		NULL,	_O_VERBOSE},
	{"debug",				no_argument,		NULL,	_O_DEBUG},
	{"force-log-colors",	no_argument,		NULL,	_O_FORCE_LOG_COLORS},
	{"no-log-colors",		no_argument,		NULL,	_O_NO_LOG_COLORS},

	{"help",				no_argument,		NULL,	_O_HELP},
	{"version",				no_argument,		NULL,	_O_VERSION},

	{NULL, 0, NULL, 0},
};


volatile bool global_stop = false;


typedef struct {
	void *v_output;
	void (*write)(void *v_output, const frame_s *frame);
	void (*destroy)(void *v_output);
} _output_context_s;


static void _signal_handler(int signum);
static void _install_signal_handlers(void);

static int _dump_sink(
	const char *sink_name, unsigned sink_timeout,
	long long count, long double interval,
	_output_context_s *ctx);

static void _help(FILE *fp);


int main(int argc, char *argv[]) {
	LOGGING_INIT;
	A_THREAD_RENAME("main");

	char *sink_name = NULL;
	unsigned sink_timeout = 1;
	char *output_path = NULL;
	bool output_json = false;
	long long count = 0;
	long double interval = 0;

#	define OPT_SET(_dest, _value) { \
			_dest = _value; \
			break; \
		}

#	define OPT_NUMBER(_name, _dest, _min, _max, _base) { \
			errno = 0; char *_end = NULL; long long _tmp = strtoll(optarg, &_end, _base); \
			if (errno || *_end || _tmp < _min || _tmp > _max) { \
				printf("Invalid value for '%s=%s': min=%lld, max=%lld\n", _name, optarg, (long long)_min, (long long)_max); \
				return 1; \
			} \
			_dest = _tmp; \
			break; \
		}

#	define OPT_LDOUBLE(_name, _dest, _min, _max) { \
			errno = 0; char *_end = NULL; long double _tmp = strtold(optarg, &_end); \
			if (errno || *_end || _tmp < _min || _tmp > _max) { \
				printf("Invalid value for '%s=%s': min=%Lf, max=%Lf\n", _name, optarg, (long double)_min, (long double)_max); \
				return 1; \
			} \
			_dest = _tmp; \
			break; \
		}

	char short_opts[128];
	build_short_options(_LONG_OPTS, short_opts, 128);

	for (int ch; (ch = getopt_long(argc, argv, short_opts, _LONG_OPTS, NULL)) >= 0;) {
		switch (ch) {
			case _O_SINK:			OPT_SET(sink_name, optarg);
			case _O_SINK_TIMEOUT:	OPT_NUMBER("--sink-timeout", sink_timeout, 1, 60, 0);
			case _O_OUTPUT:			OPT_SET(output_path, optarg);
			case _O_OUTPUT_JSON:	OPT_SET(output_json, true);
			case _O_COUNT:			OPT_NUMBER("--count", count, 0, LLONG_MAX, 0);
			case _O_INTERVAL:		OPT_LDOUBLE("--interval", interval, 0, 60);

			case _O_LOG_LEVEL:			OPT_NUMBER("--log-level", us_log_level, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, 0);
			case _O_PERF:				OPT_SET(us_log_level, LOG_LEVEL_PERF);
			case _O_VERBOSE:			OPT_SET(us_log_level, LOG_LEVEL_VERBOSE);
			case _O_DEBUG:				OPT_SET(us_log_level, LOG_LEVEL_DEBUG);
			case _O_FORCE_LOG_COLORS:	OPT_SET(us_log_colored, true);
			case _O_NO_LOG_COLORS:		OPT_SET(us_log_colored, false);

			case _O_HELP:		_help(stdout); return 0;
			case _O_VERSION:	puts(VERSION); return 0;

			case 0:		break;
			default:	return 1;
		}
	}

#	undef OPT_LDOUBLE
#	undef OPT_NUMBER
#	undef OPT_SET

	if (sink_name == NULL || sink_name[0] == '\0') {
		puts("Missing option --sink. See --help for details.");
		return 1;
	}

	_output_context_s ctx;
	MEMSET_ZERO(ctx);

	if (output_path && output_path[0] != '\0') {
		if ((ctx.v_output = (void *)output_file_init(output_path, output_json)) == NULL) {
			return 1;
		}
		ctx.write = output_file_write;
		ctx.destroy = output_file_destroy;
	}

	_install_signal_handlers();
	int retval = abs(_dump_sink(sink_name, sink_timeout, count, interval, &ctx));
	if (ctx.v_output && ctx.destroy) {
		ctx.destroy(ctx.v_output);
	}
	return retval;
}


static void _signal_handler(int signum) {
	switch (signum) {
		case SIGTERM:	LOG_INFO_NOLOCK("===== Stopping by SIGTERM ====="); break;
		case SIGINT:	LOG_INFO_NOLOCK("===== Stopping by SIGINT ====="); break;
		case SIGPIPE:	LOG_INFO_NOLOCK("===== Stopping by SIGPIPE ====="); break;
		default:		LOG_INFO_NOLOCK("===== Stopping by %d =====", signum); break;
	}
	global_stop = true;
}

static void _install_signal_handlers(void) {
	struct sigaction sig_act;
	MEMSET_ZERO(sig_act);

	assert(!sigemptyset(&sig_act.sa_mask));
	sig_act.sa_handler = _signal_handler;
	assert(!sigaddset(&sig_act.sa_mask, SIGINT));
	assert(!sigaddset(&sig_act.sa_mask, SIGTERM));
	assert(!sigaddset(&sig_act.sa_mask, SIGPIPE));

	LOG_DEBUG("Installing SIGINT handler ...");
	assert(!sigaction(SIGINT, &sig_act, NULL));

	LOG_DEBUG("Installing SIGTERM handler ...");
	assert(!sigaction(SIGTERM, &sig_act, NULL));

	LOG_DEBUG("Installing SIGTERM handler ...");
	assert(!sigaction(SIGPIPE, &sig_act, NULL));
}

static int _dump_sink(
	const char *sink_name, unsigned sink_timeout,
	long long count, long double interval,
	_output_context_s *ctx) {

	if (count == 0) {
		count = -1;
	}

	useconds_t interval_us = interval * 1000000;

	frame_s *frame = frame_init();
	memsink_s *sink = NULL;

	if ((sink = memsink_init("input", sink_name, false, 0, false, 0, sink_timeout)) == NULL) {
		goto error;
	}

	unsigned fps = 0;
	unsigned fps_accum = 0;
	long long fps_second = 0;

	long double last_ts = 0;

	while (!global_stop) {
		int error = memsink_client_get(sink, frame);
		if (error == 0) {
			const long double now = get_now_monotonic();
			const long long now_second = floor_ms(now);

			char fourcc_str[8];
			LOG_VERBOSE("Frame: size=%zu, res=%ux%u, fourcc=%s, stride=%u, online=%d, key=%d, latency=%.3Lf, diff=%.3Lf",
				frame->used, frame->width, frame->height,
				fourcc_to_string(frame->format, fourcc_str, 8),
				frame->stride, frame->online, frame->key,
				now - frame->grab_ts, (last_ts ? now - last_ts : 0));
			last_ts = now;

			LOG_DEBUG("       grab_ts=%.3Lf, encode_begin_ts=%.3Lf, encode_end_ts=%.3Lf",
				frame->grab_ts, frame->encode_begin_ts, frame->encode_end_ts);

			if (now_second != fps_second) {
				fps = fps_accum;
				fps_accum = 0;
				fps_second = now_second;
				LOG_PERF_FPS("A new second has come; captured_fps=%u", fps);
			}
			fps_accum += 1;

			if (ctx->v_output) {
				ctx->write(ctx->v_output, frame);
			}

			if (count >= 0) {
				--count;
				if (count <= 0) {
					break;
				}
			}

			if (interval_us > 0) {
				usleep(interval_us);
			}
		} else if (error == -2) {
			usleep(1000);
		} else {
			goto error;
		}
	}

	int retval = 0;
	goto ok;

	error:
		retval = -1;

	ok:
		if (sink) {
			memsink_destroy(sink);
		}
		frame_destroy(frame);

		LOG_INFO("Bye-bye");
		return retval;
}

static void _help(FILE *fp) {
#	define SAY(_msg, ...) fprintf(fp, _msg "\n", ##__VA_ARGS__)
	SAY("\nuStreamer-dump - Dump uStreamer's memory sink to file");
	SAY("═════════════════════════════════════════════════════");
	SAY("Version: %s; license: GPLv3", VERSION);
	SAY("Copyright (C) 2018-2021 Maxim Devaev <mdevaev@gmail.com>\n");
	SAY("Example:");
	SAY("════════");
	SAY("    ustreamer-dump --sink test --output - \\");
	SAY("        | ffmpeg -use_wallclock_as_timestamps 1 -i pipe: -c:v libx264 test.mp4\n");
	SAY("Sink options:");
	SAY("═════════════");
	SAY("    -s|--sink <name>  ──────── Memory sink ID. No default.\n");
	SAY("    -t|--sink-timeout <sec>  ─ Timeout for the upcoming frame. Default: 1.\n");
	SAY("    -o|--output <filename> ─── Filename to dump output to. Use '-' for stdout. Default: just consume the sink.\n");
	SAY("    -j|--output-json  ──────── Format output as JSON. Required option --output. Default: disabled.\n");
	SAY("    -c|--count  <N>  ───────── Limit the number of frames. Default: 0 (infinite).\n");
	SAY("    -i|--interval <sec>  ───── Delay between reading frames (float). Default: 0.\n");
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
#	undef SAY
}
