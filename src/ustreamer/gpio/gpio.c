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


#include "gpio.h"


us_gpio_s us_g_gpio = {
	.path = "/dev/gpiochip0",
	.consumer_prefix = "ustreamer",

#	define MAKE_OUTPUT(x_role) { \
		.pin = -1, \
		.role = x_role, \
		.consumer = NULL, \
		.line = NULL, \
		.state = false \
	}

	.prog_running = MAKE_OUTPUT("prog-running"),
	.stream_online = MAKE_OUTPUT("stream-online"),
	.has_http_clients = MAKE_OUTPUT("has-http-clients"),

#	undef MAKE_OUTPUT

	// mutex uninitialized
	.chip = NULL
};


static void _gpio_output_init(us_gpio_output_s *output);
static void _gpio_output_destroy(us_gpio_output_s *output);


void us_gpio_init(void) {
	assert(us_g_gpio.chip == NULL);
	if (
		us_g_gpio.prog_running.pin >= 0
		|| us_g_gpio.stream_online.pin >= 0
		|| us_g_gpio.has_http_clients.pin >= 0
	) {
		US_MUTEX_INIT(us_g_gpio.mutex);
		US_LOG_INFO("GPIO: Using chip device: %s", us_g_gpio.path);
		if ((us_g_gpio.chip = gpiod_chip_open(us_g_gpio.path)) != NULL) {
			_gpio_output_init(&us_g_gpio.prog_running);
			_gpio_output_init(&us_g_gpio.stream_online);
			_gpio_output_init(&us_g_gpio.has_http_clients);
		} else {
			US_LOG_PERROR("GPIO: Can't initialize chip device %s", us_g_gpio.path);
		}
	}
}

void us_gpio_destroy(void) {
	_gpio_output_destroy(&us_g_gpio.prog_running);
	_gpio_output_destroy(&us_g_gpio.stream_online);
	_gpio_output_destroy(&us_g_gpio.has_http_clients);
	if (us_g_gpio.chip != NULL) {
		gpiod_chip_close(us_g_gpio.chip);
		us_g_gpio.chip = NULL;
		US_MUTEX_DESTROY(us_g_gpio.mutex);
	}
}

int us_gpio_inner_set(us_gpio_output_s *output, bool state) {
	int retval = 0;

	assert(us_g_gpio.chip != NULL);
	assert(output->line != NULL);
	assert(output->state != state); // Must be checked in macro for the performance
	US_MUTEX_LOCK(us_g_gpio.mutex);

	if (gpiod_line_set_value(output->line, (int)state) < 0) { \
		US_LOG_PERROR("GPIO: Can't write value %d to line %s (will be disabled)", state, output->consumer); \
		_gpio_output_destroy(output);
		retval = -1;
	}

	US_MUTEX_UNLOCK(us_g_gpio.mutex);
	return retval;
}

static void _gpio_output_init(us_gpio_output_s *output) {
	assert(us_g_gpio.chip != NULL);
	assert(output->line == NULL);

	US_ASPRINTF(output->consumer, "%s::%s", us_g_gpio.consumer_prefix, output->role);

	if (output->pin >= 0) {
		if ((output->line = gpiod_chip_get_line(us_g_gpio.chip, output->pin)) != NULL) {
			if (gpiod_line_request_output(output->line, output->consumer, 0) < 0) {
				US_LOG_PERROR("GPIO: Can't request pin=%d as %s", output->pin, output->consumer);
				_gpio_output_destroy(output);
			}
		} else {
			US_LOG_PERROR("GPIO: Can't get pin=%d as %s", output->pin, output->consumer);
		}
	}
}

static void _gpio_output_destroy(us_gpio_output_s *output) {
	if (output->line != NULL) {
		gpiod_line_release(output->line);
		output->line = NULL;
	}
	if (output->consumer != NULL) {
		free(output->consumer);
		output->consumer = NULL;
	}
	output->state = false;
}
