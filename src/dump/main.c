#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include "../libs/config.h"
#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"


enum _OPT_VALUES {
	_O_SINK = 's',
	_O_TIMEOUT = 't',
	_O_OUTPUT = 'o',

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
	{"output",				no_argument,		NULL,	_O_OUTPUT},
	{"timeout",				required_argument,	NULL,	_O_TIMEOUT},

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


volatile bool stop = false;


static void _signal_handler(int signum);
static void _install_signal_handlers(void);
static int _dump_sink(const char *sink_name, const char *output_path, unsigned timeout);
static void _help(FILE *fp);


int main(int argc, char *argv[]) {
	LOGGING_INIT;
	A_THREAD_RENAME("main");

	char *sink_name = NULL;
	char *output_path = NULL;
	unsigned timeout = 1;

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

	for (int ch; (ch = getopt_long(argc, argv, "s:o:t:hv", _LONG_OPTS, NULL)) >= 0;) {
		switch (ch) {
			case _O_SINK:		OPT_SET(sink_name, optarg);
			case _O_OUTPUT:		OPT_SET(output_path, optarg);
			case _O_TIMEOUT:	OPT_NUMBER("--timeout", timeout, 1, 60, 0);

			case _O_LOG_LEVEL:			OPT_NUMBER("--log-level", log_level, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, 0);
			case _O_PERF:				OPT_SET(log_level, LOG_LEVEL_PERF);
			case _O_VERBOSE:			OPT_SET(log_level, LOG_LEVEL_VERBOSE);
			case _O_DEBUG:				OPT_SET(log_level, LOG_LEVEL_DEBUG);
			case _O_FORCE_LOG_COLORS:	OPT_SET(log_colored, true);
			case _O_NO_LOG_COLORS:		OPT_SET(log_colored, false);

			case _O_HELP:		_help(stdout); return 0;
			case _O_VERSION:	puts(VERSION); return 0;

			case 0:		break;
			default:	_help(stderr); return 1;
		}
	}

#	undef OPT_NUMBER
#	undef OPT_SET

	if (sink_name == NULL || sink_name[0] == '\0') {
		puts("Missing option --sink. See --help for details.");
		return 1;
	}

	_install_signal_handlers();
	return abs(_dump_sink(sink_name, output_path, timeout));
}


static void _signal_handler(int signum) {
	switch (signum) {
		case SIGTERM:	LOG_INFO_NOLOCK("===== Stopping by SIGTERM ====="); break;
		case SIGINT:	LOG_INFO_NOLOCK("===== Stopping by SIGINT ====="); break;
		case SIGPIPE:	LOG_INFO_NOLOCK("===== Stopping by SIGPIPE ====="); break;
		default:		LOG_INFO_NOLOCK("===== Stopping by %d =====", signum); break;
	}
	stop = true;
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

static int _dump_sink(const char *sink_name, const char *output_path, unsigned timeout) {
	frame_s *frame = frame_init("input");
	FILE *output_fp = NULL;
	memsink_s *sink = NULL;

	if (output_path && output_path[0] != '\0') {
		if (!strcmp(output_path, "-")) {
			LOG_INFO("Using output: <stdout>");
			output_fp = stdout;
		} else {
			LOG_INFO("Using output: %s", output_path);
			if ((output_fp = fopen(output_path, "wb")) == NULL) {
				LOG_PERROR("Can't open output file");
				goto error;
			}
		}
	}

	if ((sink = memsink_init("input", sink_name, false, 0, false, timeout)) == NULL) {
		goto error;
	}

	unsigned fps = 0;
	unsigned fps_accum = 0;
	long long fps_second = 0;

	while (!stop) {
		int error = memsink_client_get(sink, frame);
		if (error == 0) {
			const long double now = get_now_monotonic();
			const long long now_second = floor_ms(now);

			LOG_VERBOSE("Frame: size=%zu, resolution=%ux%u, format=%u, stride=%u, online=%d",
				frame->used, frame->width, frame->height, frame->format, frame->stride, frame->online);

			LOG_DEBUG("       grab_ts=%.3Lf, encode_begin_ts=%.3Lf, encode_end_ts=%.3Lf, latency=%.3Lf",
				frame->grab_ts, frame->encode_begin_ts, frame->encode_end_ts, now - frame->grab_ts);

			if (now_second != fps_second) {
				fps = fps_accum;
				fps_accum = 0;
				fps_second = now_second;
				LOG_PERF_FPS("A new second has come; captured_fps=%u", fps);
			}
			fps_accum += 1;

			if (output_fp) {
				fwrite(frame->data, 1, frame->used, output_fp);
				fflush(output_fp);
			}
		} else if (error != -2) {
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
		if (output_fp && output_fp != stdout) {
			if (fclose(output_fp) < 0) {
				LOG_PERROR("Can't close output file");
			}
		}
		frame_destroy(frame);

		LOG_INFO("Bye-bye");
		return retval;
}

static void _help(FILE *fp) {
#	define SAY(_msg, ...) fprintf(fp, _msg "\n", ##__VA_ARGS__)
	SAY("\nuStreamer-dump - Dump uStreamer's memory sink to file");
	SAY("═══════════════════════════════════════════════════════");
	SAY("Version: %s; license: GPLv3", VERSION);
	SAY("Copyright (C) 2018 Maxim Devaev <mdevaev@gmail.com>\n");
	SAY("Example:");
	SAY("════════");
	SAY("    ustreamer-dump --sink test -o - \\");
	SAY("        | ffmpeg -use_wallclock_as_timestamps 1 -i pipe: -c:v libx264 test.mp4\n");
	SAY("Sink options:");
	SAY("═════════════");
	SAY("    -s|--sink <name>  ─── Memory sink ID. No default.\n");
	SAY("    -t|--timeout <sec>  ─ Timeout for the upcoming frame. Default: 1.\n");
	SAY("    -o|--output  ──────── Filename to dump. Use '-' for stdout. Default: just consume the sink.\n");
	SAY("Logging options:");
	SAY("════════════════");
	SAY("    --log-level <N>  ──── Verbosity level of messages from 0 (info) to 3 (debug).");
	SAY("                          Enabling debugging messages can slow down the program.");
	SAY("                          Available levels: 0 (info), 1 (performance), 2 (verbose), 3 (debug).");
	SAY("                          Default: %d.\n", log_level);
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
