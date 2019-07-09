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


#pragma once

#include <stdlib.h>

#include <wiringPi.h>

#include "logging.h"


int gpio_pin_prog_running;
int gpio_pin_stream_online;
int gpio_pin_has_http_clients;
int gpio_pin_workers_busy_at;


#define GPIO_INIT { \
		gpio_pin_prog_running = -1; \
		gpio_pin_stream_online = -1; \
		gpio_pin_has_http_clients = -1; \
		gpio_pin_workers_busy_at = -1; \
	}

#define GPIO_INIT_PIN(_role, _offset) { \
		if (gpio_pin_##_role >= 0) { \
			pinMode(gpio_pin_##_role + _offset, OUTPUT); \
			digitalWrite(gpio_pin_##_role + _offset, LOW); \
			if (_offset == 0) { \
				LOG_INFO("GPIO: Using pin %d as %s", gpio_pin_##_role, #_role); \
			} else { \
				LOG_INFO("GPIO: Using pin %d+%d as %s", gpio_pin_##_role, _offset, #_role); \
			} \
		} \
	}

#define GPIO_INIT_PINOUT { \
		if ( \
			gpio_pin_prog_running >= 0 \
			|| gpio_pin_stream_online >= 0 \
			|| gpio_pin_has_http_clients >= 0 \
			|| gpio_pin_workers_busy_at >= 0 \
		) { \
			LOG_INFO("GPIO: Using wiringPi"); \
			if (wiringPiSetupGpio() < 0) { \
				LOG_PERROR("GPIO: Can't initialize wiringPi"); \
				exit(1); \
			} else { \
				GPIO_INIT_PIN(prog_running, 0); \
				GPIO_INIT_PIN(stream_online, 0); \
				GPIO_INIT_PIN(has_http_clients, 0); \
				GPIO_INIT_PIN(workers_busy_at, 0); \
			} \
		} \
	}

#define GPIO_SET_STATE(_role, _offset, _state) { \
		if (gpio_pin_##_role >= 0) { \
			if (_offset == 0) { \
				LOG_DEBUG("GPIO: Writing %d to pin %d (%s)", _state, gpio_pin_##_role, #_role); \
			} else { \
				LOG_DEBUG("GPIO: Writing %d to pin %d+%d (%s)", _state, gpio_pin_##_role, _offset, #_role); \
			} \
			digitalWrite(gpio_pin_##_role + _offset, _state); \
		} \
	}

#define GPIO_SET_LOW(_role)		GPIO_SET_STATE(_role, 0, LOW)
#define GPIO_SET_HIGH(_role)	GPIO_SET_STATE(_role, 0, HIGH)

#define GPIO_SET_LOW_AT(_role, _offset)		GPIO_SET_STATE(_role, _offset, LOW)
#define GPIO_SET_HIGH_AT(_role, _offset)	GPIO_SET_STATE(_role, _offset, HIGH)
