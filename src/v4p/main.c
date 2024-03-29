/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <stdatomic.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "../libs/types.h"
#include "../libs/errors.h"
#include "../libs/const.h"
#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/capture.h"
#include "../libs/signal.h"
#include "../libs/options.h"

#include "../libs/drm/drm.h"


enum _OPT_VALUES {
	_O_UNIX_FOLLOW = 'f',

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
	{"unix-follow",			required_argument,	NULL,	_O_UNIX_FOLLOW},

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


volatile atomic_bool _g_stop = false;
atomic_bool _g_ustreamer_online = false;


static void _signal_handler(int signum);

static void _main_loop();
static void *_follower_thread(void *v_unix_follow);
static void _slowdown(void);

static void _help(FILE *fp);


int main(int argc, char *argv[]) {
	US_LOGGING_INIT;
	US_THREAD_RENAME("main");

	char *unix_follow = NULL;

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

	char short_opts[128];
	us_build_short_options(_LONG_OPTS, short_opts, 128);

	for (int ch; (ch = getopt_long(argc, argv, short_opts, _LONG_OPTS, NULL)) >= 0;) {
		switch (ch) {
			case _O_UNIX_FOLLOW:	OPT_SET(unix_follow, optarg);

			case _O_LOG_LEVEL:			OPT_NUMBER("--log-level", us_g_log_level, US_LOG_LEVEL_INFO, US_LOG_LEVEL_DEBUG, 0);
			case _O_PERF:				OPT_SET(us_g_log_level, US_LOG_LEVEL_PERF);
			case _O_VERBOSE:			OPT_SET(us_g_log_level, US_LOG_LEVEL_VERBOSE);
			case _O_DEBUG:				OPT_SET(us_g_log_level, US_LOG_LEVEL_DEBUG);
			case _O_FORCE_LOG_COLORS:	OPT_SET(us_g_log_colored, true);
			case _O_NO_LOG_COLORS:		OPT_SET(us_g_log_colored, false);

			case _O_HELP:		_help(stdout); return 0;
			case _O_VERSION:	puts(US_VERSION); return 0;

			case 0:		break;
			default:	return 1;
		}
	}

#	undef OPT_NUMBER
#	undef OPT_SET

	us_install_signals_handler(_signal_handler, false);

	pthread_t follower_tid;
	if (unix_follow != NULL) {
		US_THREAD_CREATE(follower_tid, _follower_thread, unix_follow);
	}
	_main_loop();
	if (unix_follow != NULL) {
		US_THREAD_JOIN(follower_tid);
	}

	US_LOGGING_DESTROY;
	return 0;
}

static void _signal_handler(int signum) {
	char *const name = us_signum_to_string(signum);
	US_LOG_INFO_NOLOCK("===== Stopping by %s =====", name);
	free(name);
	atomic_store(&_g_stop, true);
}

static void _main_loop(void) {
	us_drm_s *drm = us_drm_init();

	us_capture_s *cap = us_capture_init();
	cap->path = "/dev/kvmd-video";
	cap->n_bufs = 6;
	cap->format = V4L2_PIX_FMT_RGB24;
	cap->format_swap_rgb = true;
	cap->dv_timings = true;
	cap->persistent = true;
	cap->dma_export = true;
	cap->dma_required = true;

	int once = 0;
	while (!atomic_load(&_g_stop)) {
#		define CHECK(x_arg) if ((x_arg) < 0) { goto close; }

		if (drm->run->opened <= 0) {
			CHECK(us_drm_open(drm, NULL));
		}

		if (atomic_load(&_g_ustreamer_online)) {
			US_ONCE({ US_LOG_INFO("DRM: Online stream is active, pausing the service ..."); });
			CHECK(us_drm_wait_for_vsync(drm));
			CHECK(us_drm_expose_stub(drm, US_DRM_STUB_BUSY, NULL));
			_slowdown();
			continue;
		}

		if (us_capture_open(cap) < 0) {
			CHECK(us_drm_ensure_no_signal(drm));
			_slowdown();
			continue;
		}

		once = 0;
		us_drm_close(drm);
		CHECK(us_drm_open(drm, cap));

		us_capture_hwbuf_s *prev_hw = NULL;
		while (!atomic_load(&_g_stop)) {
			if (atomic_load(&_g_ustreamer_online)) {
				goto close;
			}

			CHECK(us_drm_wait_for_vsync(drm));

			if (prev_hw != NULL) {
				CHECK(us_capture_hwbuf_release(cap, prev_hw));
				prev_hw = NULL;
			}

			us_capture_hwbuf_s *hw;
			switch (us_capture_hwbuf_grab(cap, &hw)) {
				case 0 ... INT_MAX: break; // Grabbed buffer number
				case US_ERROR_NO_DATA: continue; // Broken frame
				default: goto close; // Any error
			}

			if (drm->run->opened == 0) {
				CHECK(us_drm_expose_dma(drm, hw));
				prev_hw = hw;
				continue;
			}

			CHECK(us_drm_expose_stub(drm, drm->run->opened, cap));
			CHECK(us_capture_hwbuf_release(cap, hw));
			_slowdown();
		}

	close:
		us_drm_close(drm);
		us_capture_close(cap);
		_slowdown();

#		undef CHECK
	}

	us_capture_destroy(cap);
	us_drm_destroy(drm);
}

static void *_follower_thread(void *v_unix_follow) {
	US_THREAD_SETTLE("follower");
	const char *path = v_unix_follow;
	assert(path != NULL);

	while (!atomic_load(&_g_stop)) {
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		assert(fd >= 0);

		struct sockaddr_un addr = {0};
		strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
		addr.sun_family = AF_UNIX;

		const bool online = !connect(fd, (struct sockaddr*)&addr, sizeof(addr));
		atomic_store(&_g_ustreamer_online, online);
		US_CLOSE_FD(fd); // cppcheck-suppress unreadVariable

		usleep(200 * 1000);
	}
	return NULL;
}

static void _slowdown(void) {
	if (!atomic_load(&_g_stop)) {
		usleep(500 * 1000);
	}
}

static void _help(FILE *fp) {
#	define SAY(_msg, ...) fprintf(fp, _msg "\n", ##__VA_ARGS__)
	SAY("\nuStreamer-V4P - Video passthrough for PiKVM V4 Plus");
	SAY("═════════════════════════════════════════════════════");
	SAY("Version: %s; license: GPLv3", US_VERSION);
	SAY("Copyright (C) 2018-2024 Maxim Devaev <mdevaev@gmail.com>\n");
	SAY("Example:");
	SAY("════════");
	SAY("    ustreamer-v4p\n");
	SAY("Passthrough options:");
	SAY("════════════════════");
	SAY("    -f|--unix-follow <path>  ──────── Pause the process if the specified socked exists.\n");
	SAY("Logging options:");
	SAY("════════════════");
	SAY("    --log-level <N>  ──── Verbosity level of messages from 0 (info) to 3 (debug).");
	SAY("                          Enabling debugging messages can slow down the program.");
	SAY("                          Available levels: 0 (info), 1 (performance), 2 (verbose), 3 (debug).");
	SAY("                          Default: %d.\n", us_g_log_level);
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
