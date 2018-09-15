#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>


bool debug;


#define SEP_INFO(_x_ch) \
	{ for (int _i = 0; _i < 80; ++_i) putchar(_x_ch); putchar('\n'); }

#define LOG_INFO(_x_msg, ...) \
	printf("-- INFO  -- " _x_msg "\n", ##__VA_ARGS__)

#define LOG_DEBUG(_x_msg, ...) \
		if (debug) { printf("   DEBUG -- " _x_msg "\n", ##__VA_ARGS__); }

#define SEP_DEBUG(_x_ch) \
		if (debug) { SEP_INFO(_x_ch); }

#define LOG_ERROR(_x_msg, ...) \
	printf("** ERROR -- " _x_msg "\n", ##__VA_ARGS__)

#define LOG_PERROR(_x_msg, ...) \
	printf("** ERROR -- " _x_msg ": %s\n", ##__VA_ARGS__, strerror(errno))


#define MEMSET_ZERO(_x_obj) memset(&(_x_obj), 0, sizeof(_x_obj))

#define XIOCTL_RETRIES 4


int xioctl(const int fd, const int request, void *arg);
