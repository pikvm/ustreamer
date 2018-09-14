#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

#include "device.h"
#include "capture.h"
#include "tools.h"


static const char _short_opts[] = "hd:f:s:e:tb:q:";
static const struct option _long_opts[] = {
	{"help",				no_argument,		NULL,	'h'},
	{"device",				required_argument,	NULL,	'd'},
	{"format",				required_argument,	NULL,	'f'},
	{"tv-standard",			required_argument,	NULL,	's'},
	{"every-frame",			required_argument,	NULL,	'e'},
	{"min-frame-size",		required_argument,	NULL,	'z'},
	{"dv-timings",			no_argument,		NULL,	't'},
	{"buffers",				required_argument,	NULL,	'b'},
	{"jpeg-quality",		required_argument,	NULL,	'q'},
	{"width",				required_argument,	NULL,	1000},
	{"height",				required_argument,	NULL,	1001},
	{"v4l2-timeout",		required_argument,	NULL,	1002},
	{"v4l2-error-timeout",	required_argument,	NULL,	1003},
	{NULL, 0, NULL, 0},
};


static void _help(int exit_code) {
	printf("No manual yet\n");
	exit(exit_code);
}

static void _parse_options(int argc, char *argv[], struct device *dev) {
#	define OPT_ARG(_dest) \
		{ _dest = optarg; break; }

#	define OPT_TRUE(_dest) \
		{ _dest = true; break; }

#	define OPT_UNSIGNED(_dest, _name) \
		{ int _tmp = strtol(optarg, NULL, 0); \
		if (errno || _tmp < 0) \
		{ printf("Invalid value for: %s\n", _name); exit(EXIT_FAILURE); } \
		_dest = _tmp; break; }

#	define OPT_PARSE(_dest, _func, _invalid, _name) \
		{ if ((_dest = _func(optarg)) == _invalid) \
		{ printf("Unknown " _name ": %s\n", optarg); exit(EXIT_FAILURE); } \
		break; }

	int index;
	int ch;

	LOG_DEBUG("Parsing CLI options ...");
	while ((ch = getopt_long(argc, argv, _short_opts, _long_opts, &index)) >= 0) {
		switch (ch) {
			case 0:		break;
			case 'd':	OPT_ARG(dev->path);
#			pragma GCC diagnostic ignored "-Wsign-compare"
#			pragma GCC diagnostic push
			case 'f':	OPT_PARSE(dev->format, device_parse_format, FORMAT_UNKNOWN, "pixel format");
#			pragma GCC diagnostic pop
			case 's':	OPT_PARSE(dev->standard, device_parse_standard, STANDARD_UNKNOWN, "TV standard");
			case 'e':	OPT_UNSIGNED(dev->every_frame, "--every-frame");
			case 'z':	OPT_UNSIGNED(dev->min_frame_size, "--min-frame-size");
			case 't':	OPT_TRUE(dev->dv_timings);
			case 'b':	OPT_UNSIGNED(dev->n_buffers, "--buffers");
			case 'q':	OPT_UNSIGNED(dev->jpeg_quality, "--jpeg-quality");
			case 1000:	OPT_UNSIGNED(dev->width, "--width");
			case 1001:	OPT_UNSIGNED(dev->height, "--height");
			case 1002:	OPT_UNSIGNED(dev->timeout, "--timeout");
			case 1003:	OPT_UNSIGNED(dev->error_timeout, "--error-timeout");
			case 'h':	_help(EXIT_SUCCESS); break;
			default:	_help(EXIT_FAILURE); break;
		}
	}
}


static bool _global_stop = false;

static void _interrupt_handler(int signum) {
	LOG_INFO("===== Stopping by %s =====", strsignal(signum));
	_global_stop = true;
}


int main(int argc, char *argv[]) {
	struct device dev;
	struct device_runtime run;

	device_init(&dev, &run, &_global_stop);
	_parse_options(argc, argv, &dev);

	LOG_INFO("Installing SIGINT handler ...");
	signal(SIGINT, _interrupt_handler);

	LOG_INFO("Installing SIGTERM handler ...");
	signal(SIGTERM, _interrupt_handler);

	capture_loop(&dev);
	return 0;
}
