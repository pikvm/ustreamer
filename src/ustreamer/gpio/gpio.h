/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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
	int					pin;
	const char			*role;
	char 				*consumer;
	struct gpiod_line	*line;
	bool				state;
} us_gpio_output_s;

typedef struct {
	char	*path;
	char	*consumer_prefix;

	us_gpio_output_s	prog_running;
	us_gpio_output_s	stream_online;
	us_gpio_output_s	has_http_clients;

	pthread_mutex_t		mutex;
	struct gpiod_chip	*chip;
} us_gpio_s;


extern us_gpio_s us_g_gpio;


void us_gpio_init(void);
void us_gpio_destroy(void);
int us_gpio_inner_set(us_gpio_output_s *output, bool state);


#define SET_STATE(x_output, x_state) { \
		if (x_output.line && x_output.state != x_state) { \
			if (!us_gpio_inner_set(&x_output, x_state)) { \
				x_output.state = x_state; \
			} \
		} \
	}

INLINE void us_gpio_set_prog_running(bool state) {
	SET_STATE(us_g_gpio.prog_running, state);
}

INLINE void us_gpio_set_stream_online(bool state) {
	SET_STATE(us_g_gpio.stream_online, state);
}

INLINE void us_gpio_set_has_http_clients(bool state) {
	SET_STATE(us_g_gpio.has_http_clients, state);
}

#undef SET_STATE
