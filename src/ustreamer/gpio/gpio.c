/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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


struct gpio_t gpio = {
	.path = "/dev/gpiochip0",
	.consumer_prefix = "ustreamer",

#	define MAKE_OUTPUT(_role) { \
		.pin = -1, \
		.role = _role, \
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


static void _gpio_output_init(struct gpio_output_t *output);
static void _gpio_output_destroy(struct gpio_output_t *output);


void gpio_init(void) {
	assert(gpio.chip == NULL);
	if (
		gpio.prog_running.pin >= 0
		|| gpio.stream_online.pin >= 0
		|| gpio.has_http_clients.pin >= 0
	) {
		A_MUTEX_INIT(&gpio.mutex);
		LOG_INFO("GPIO: Using chip device: %s", gpio.path);
		if ((gpio.chip = gpiod_chip_open(gpio.path)) != NULL) {
			_gpio_output_init(&gpio.prog_running);
			_gpio_output_init(&gpio.stream_online);
			_gpio_output_init(&gpio.has_http_clients);
		} else {
			LOG_PERROR("GPIO: Can't initialize chip device %s", gpio.path);
		}
	}
}

void gpio_destroy(void) {
	_gpio_output_destroy(&gpio.prog_running);
	_gpio_output_destroy(&gpio.stream_online);
	_gpio_output_destroy(&gpio.has_http_clients);
	if (gpio.chip) {
		gpiod_chip_close(gpio.chip);
		gpio.chip = NULL;
		A_MUTEX_DESTROY(&gpio.mutex);
	}
}

int gpio_inner_set(struct gpio_output_t *output, bool state) {
	int retval = 0;

	assert(gpio.chip);
	assert(output->line);
	assert(output->state != state); // Must be checked in macro for the performance
	A_MUTEX_LOCK(&gpio.mutex);

	if (gpiod_line_set_value(output->line, (int)state) < 0) { \
		LOG_PERROR("GPIO: Can't write value %d to line %s (will be disabled)", state, output->consumer); \
		_gpio_output_destroy(output);
		retval = -1;
	}

	A_MUTEX_UNLOCK(&gpio.mutex);
	return retval;
}

static void _gpio_output_init(struct gpio_output_t *output) {
	assert(gpio.chip);
	assert(output->line == NULL);

	A_CALLOC(output->consumer, strlen(gpio.consumer_prefix) + strlen(output->role) + 16);
	sprintf(output->consumer, "%s::%s", gpio.consumer_prefix, output->role);

	if (output->pin >= 0) {
		if ((output->line = gpiod_chip_get_line(gpio.chip, output->pin)) != NULL) {
			if (gpiod_line_request_output(output->line, output->consumer, 0) < 0) {
				LOG_PERROR("GPIO: Can't request pin=%d as %s", output->pin, output->consumer);
				_gpio_output_destroy(output);
			}
		} else {
			LOG_PERROR("GPIO: Can't get pin=%d as %s", output->pin, output->consumer);
		}
	}
}

static void _gpio_output_destroy(struct gpio_output_t *output) {
	if (output->line) {
		gpiod_line_release(output->line);
		output->line = NULL;
	}
	if (output->consumer) {
		free(output->consumer);
		output->consumer = NULL;
	}
	output->state = false;
}
