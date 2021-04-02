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
} gpio_output_s;

typedef struct {
	char *path;
	char *consumer_prefix;

	gpio_output_s prog_running;
	gpio_output_s stream_online;
	gpio_output_s has_http_clients;

	pthread_mutex_t		mutex;
	struct gpiod_chip	*chip;
} gpio_s;


extern gpio_s us_gpio;


void gpio_init(void);
void gpio_destroy(void);
int gpio_inner_set(gpio_output_s *output, bool state);


#define SET_STATE(_output, _state) { \
		if (_output.line && _output.state != _state) { \
			if (!gpio_inner_set(&_output, _state)) { \
				_output.state = _state; \
			} \
		} \
	}

INLINE void gpio_set_prog_running(bool state) {
	SET_STATE(us_gpio.prog_running, state);
}

INLINE void gpio_set_stream_online(bool state) {
	SET_STATE(us_gpio.stream_online, state);
}

INLINE void gpio_set_has_http_clients(bool state) {
	SET_STATE(us_gpio.has_http_clients, state);
}

#undef SET_STATE
