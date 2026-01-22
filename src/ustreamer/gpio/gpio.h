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


#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>
#include <gpiod.h>

#include "../../libs/tools.h"
#include "../../libs/logging.h"
#include "../../libs/threading.h"


typedef struct {
	int							pin;
	const char					*role;
	char 						*consumer;
#	ifdef HAVE_GPIOD2
	struct gpiod_line_request	*line;
#	else
	struct gpiod_line			*line;
#	endif
	bool						on;
} us_gpio_output_s;

typedef struct {
	char	*path;
	char	*consumer_prefix;

	us_gpio_output_s	prog_running;
	us_gpio_output_s	stream_online;
	us_gpio_output_s	has_http_clients;

	pthread_mutex_t		mutex;

#	ifndef HAVE_GPIOD2
	struct gpiod_chip	*chip;
#	endif
	bool				initialized;
} us_gpio_s;


extern us_gpio_s us_g_gpio;


void us_gpio_init(void);
void us_gpio_destroy(void);
int us_gpio_inner_set(us_gpio_output_s *out, bool on);


#define SET_ON(x_out, x_on) { \
		if (x_out.line && x_out.on != x_on) { \
			if (!us_gpio_inner_set(&x_out, x_on)) { \
				x_out.on = x_on; \
			} \
		} \
	}

INLINE void us_gpio_set_prog_running(bool on) {
	SET_ON(us_g_gpio.prog_running, on);
}

INLINE void us_gpio_set_stream_online(bool on) {
	SET_ON(us_g_gpio.stream_online, on);
}

INLINE void us_gpio_set_has_http_clients(bool on) {
	SET_ON(us_g_gpio.has_http_clients, on);
}

#undef SET_ON
