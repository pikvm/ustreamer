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
#include <sys/param.h>

#include "tools.h"
#include "device.h"
#include "capture.h"


static const char _short_opts[] = "hd:f:s:e:tn:q:";
static const struct option _long_opts[] = {
	{"help",				no_argument,		NULL,	'h'},
	{"device",				required_argument,	NULL,	'd'},
	{"format",				required_argument,	NULL,	'f'},
	{"tv-standard",			required_argument,	NULL,	's'},
	{"every-frame",			required_argument,	NULL,	'e'},
	{"min-frame-size",		required_argument,	NULL,	'z'},
	{"dv-timings",			no_argument,		NULL,	't'},
	{"buffers",				required_argument,	NULL,	'n'},
	{"jpeg-quality",		required_argument,	NULL,	'q'},
	{"width",				required_argument,	NULL,	1000},
	{"height",				required_argument,	NULL,	1001},
	{"v4l2-timeout",		required_argument,	NULL,	1002},
	{"v4l2-error-timeout",	required_argument,	NULL,	1003},
	{"debug",				no_argument,		NULL,	5000},
	{NULL, 0, NULL, 0},
};

static void _help(int exit_code) {
	printf("No manual yet\n");
	exit(exit_code);
}

static void _parse_options(int argc, char *argv[], struct device_t *dev) {
#	define OPT_ARG(_dest) \
		{ _dest = optarg; break; }

#	define OPT_TRUE(_dest) \
		{ _dest = true; break; }

#	define OPT_UNSIGNED(_dest, _name, _min) \
		{ int _tmp = strtol(optarg, NULL, 0); \
		if (errno || _tmp < _min) \
		{ printf("Invalid value for '%s=%u'; minimal=%u\n", _name, _tmp, _min); exit(EXIT_FAILURE); } \
		_dest = _tmp; break; }

#	define OPT_PARSE(_dest, _func, _invalid, _name) \
		{ if ((_dest = _func(optarg)) == _invalid) \
		{ printf("Unknown " _name ": %s\n", optarg); exit(EXIT_FAILURE); } \
		break; }

	int index;
	int ch;

	debug = false;
	while ((ch = getopt_long(argc, argv, _short_opts, _long_opts, &index)) >= 0) {
		switch (ch) {
			case 0:		break;
			case 'd':	OPT_ARG(dev->path);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case 'f':	OPT_PARSE(dev->format, device_parse_format, FORMAT_UNKNOWN, "pixel format");
#			pragma GCC diagnostic pop
			case 's':	OPT_PARSE(dev->standard, device_parse_standard, STANDARD_UNKNOWN, "TV standard");
			case 'e':	OPT_UNSIGNED(dev->every_frame, "--every-frame", 0);
			case 'z':	OPT_UNSIGNED(dev->min_frame_size, "--min-frame-size", 0);
			case 't':	OPT_TRUE(dev->dv_timings);
			case 'n':	OPT_UNSIGNED(dev->n_buffers, "--buffers", 1);
			case 'q':	OPT_UNSIGNED(dev->jpeg_quality, "--jpeg-quality", 1);
			case 1000:	OPT_UNSIGNED(dev->width, "--width", 320);
			case 1001:	OPT_UNSIGNED(dev->height, "--height", 180);
			case 1002:	OPT_UNSIGNED(dev->timeout, "--timeout", 1);
			case 1003:	OPT_UNSIGNED(dev->error_timeout, "--error-timeout", 1);
			case 5000:	OPT_TRUE(debug);
			case 'h':	_help(EXIT_SUCCESS); break;
			default:	_help(EXIT_FAILURE); break;
		}
	}
}

struct threads_context {
	struct device_t	*dev;
	sig_atomic_t	*volatile global_stop;
};

static void *_capture_loop_thread(void *v_ctx_ptr) {
	struct threads_context *ctx = (struct threads_context *)v_ctx_ptr;
	sigset_t mask;

	assert(!sigemptyset(&mask));
	assert(!sigaddset(&mask, SIGINT));
	assert(!sigaddset(&mask, SIGTERM));
	assert(!pthread_sigmask(SIG_BLOCK, &mask, NULL));

	capture_loop(ctx->dev, (sig_atomic_t *volatile)ctx->global_stop);
	return NULL;
}

static volatile sig_atomic_t _global_stop = 0;

static void _interrupt_handler(int signum) {
	LOG_INFO("===== Stopping by %s =====", strsignal(signum));
	_global_stop = 1;
}

int main(int argc, char *argv[]) {
	struct device_t dev;
	struct device_runtime_t run;

	pthread_t capture_loop_tid;
	struct threads_context ctx = {&dev,	(sig_atomic_t *volatile)&_global_stop};
	struct sigaction sig_act;

	device_init(&dev, &run);
	_parse_options(argc, argv, &dev);

	MEMSET_ZERO(sig_act);
	assert(!sigemptyset(&sig_act.sa_mask));
	sig_act.sa_handler = _interrupt_handler;
	assert(!sigaddset(&sig_act.sa_mask, SIGINT));
	assert(!sigaddset(&sig_act.sa_mask, SIGTERM));

	LOG_INFO("Installing SIGINT handler ...");
	assert(!sigaction(SIGINT, &sig_act, NULL));

	LOG_INFO("Installing SIGTERM handler ...");
	assert(!sigaction(SIGTERM, &sig_act, NULL));

	A_PTHREAD_CREATE(&capture_loop_tid, _capture_loop_thread, (void *)&ctx);
	A_PTHREAD_JOIN(capture_loop_tid);

	return 0;
}
