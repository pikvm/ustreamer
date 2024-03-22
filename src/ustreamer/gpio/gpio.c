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

#	ifndef HAVE_GPIOD2
	.chip = NULL,
#	endif
	.initialized = false,
};


static void _gpio_output_init(us_gpio_output_s *output, struct gpiod_chip *chip);
static void _gpio_output_destroy(us_gpio_output_s *output);


void us_gpio_init(void) {
#	ifndef HAVE_GPIOD2
	assert(us_g_gpio.chip == NULL);
#	endif
	if (
		us_g_gpio.prog_running.pin >= 0
		|| us_g_gpio.stream_online.pin >= 0
		|| us_g_gpio.has_http_clients.pin >= 0
	) {
		US_MUTEX_INIT(us_g_gpio.mutex);
		US_LOG_INFO("GPIO: Using chip device: %s", us_g_gpio.path);
		struct gpiod_chip *chip;
		if ((chip = gpiod_chip_open(us_g_gpio.path)) != NULL) {
			_gpio_output_init(&us_g_gpio.prog_running, chip);
			_gpio_output_init(&us_g_gpio.stream_online, chip);
			_gpio_output_init(&us_g_gpio.has_http_clients, chip);
#			ifdef HAVE_GPIOD2
			gpiod_chip_close(chip);
#			else
			us_g_gpio.chip = chip;
#			endif
			us_g_gpio.initialized = true;
		} else {
			US_LOG_PERROR("GPIO: Can't initialize chip device %s", us_g_gpio.path);
		}
	}
}

void us_gpio_destroy(void) {
	_gpio_output_destroy(&us_g_gpio.prog_running);
	_gpio_output_destroy(&us_g_gpio.stream_online);
	_gpio_output_destroy(&us_g_gpio.has_http_clients);
	if (us_g_gpio.initialized) {
#		ifndef HAVE_GPIOD2
		gpiod_chip_close(us_g_gpio.chip);
		us_g_gpio.chip = NULL;
#		endif
		US_MUTEX_DESTROY(us_g_gpio.mutex);
		us_g_gpio.initialized = false;
	}
}

int us_gpio_inner_set(us_gpio_output_s *output, bool state) {
	int retval = 0;

#	ifndef HAVE_GPIOD2
	assert(us_g_gpio.chip != NULL);
#	endif
	assert(output->line != NULL);
	assert(output->state != state); // Must be checked in macro for the performance
	US_MUTEX_LOCK(us_g_gpio.mutex);

#	ifdef HAVE_GPIOD2
	if (gpiod_line_request_set_value(output->line, output->pin, state) < 0) {
#	else
	if (gpiod_line_set_value(output->line, (int)state) < 0) {
#	endif
		US_LOG_PERROR("GPIO: Can't write value %d to line %s", state, output->consumer); \
		_gpio_output_destroy(output);
		retval = -1;
	}

	US_MUTEX_UNLOCK(us_g_gpio.mutex);
	return retval;
}

static void _gpio_output_init(us_gpio_output_s *output, struct gpiod_chip *chip) {
	assert(output->line == NULL);

	US_ASPRINTF(output->consumer, "%s::%s", us_g_gpio.consumer_prefix, output->role);

	if (output->pin >= 0) {
#		ifdef HAVE_GPIOD2
		struct gpiod_line_settings *line_settings;
		assert(line_settings = gpiod_line_settings_new());
		assert(!gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT));
		assert(!gpiod_line_settings_set_output_value(line_settings, false));

		struct gpiod_line_config *line_config;
		assert(line_config = gpiod_line_config_new());
		const unsigned offset = output->pin;
		assert(!gpiod_line_config_add_line_settings(line_config, &offset, 1, line_settings));

		struct gpiod_request_config *request_config;
		assert(request_config = gpiod_request_config_new());
		gpiod_request_config_set_consumer(request_config, output->consumer);

		if ((output->line = gpiod_chip_request_lines(chip, request_config, line_config)) == NULL) {
			US_LOG_PERROR("GPIO: Can't request pin=%d as %s", output->pin, output->consumer);
		}

		gpiod_request_config_free(request_config);
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(line_settings);

		if (output->line == NULL) {
			_gpio_output_destroy(output);
		}

#		else

		if ((output->line = gpiod_chip_get_line(chip, output->pin)) != NULL) {
			if (gpiod_line_request_output(output->line, output->consumer, 0) < 0) {
				US_LOG_PERROR("GPIO: Can't request pin=%d as %s", output->pin, output->consumer);
				_gpio_output_destroy(output);
			}
		} else {
			US_LOG_PERROR("GPIO: Can't get pin=%d as %s", output->pin, output->consumer);
		}
#		endif
	}
}

static void _gpio_output_destroy(us_gpio_output_s *output) {
	if (output->line != NULL) {
#		ifdef HAVE_GPIOD2
		gpiod_line_request_release(output->line);
#		else
		gpiod_line_release(output->line);
#		endif
		output->line = NULL;
	}
	if (output->consumer != NULL) {
		free(output->consumer);
		output->consumer = NULL;
	}
	output->state = false;
}
