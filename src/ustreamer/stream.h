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
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "../common/tools.h"
#include "../common/threading.h"
#include "../common/logging.h"

#include "picture.h"
#include "device.h"
#include "encoder.h"
#ifdef WITH_RAWSINK
#	include "../rawsink/rawsink.h"
#endif
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


struct process_t {
	atomic_bool stop;
	atomic_bool slowdown;
};

struct stream_t {
	struct picture_t	*picture;
	bool				online;
	unsigned			captured_fps;
	atomic_bool			updated;
	pthread_mutex_t		mutex;

	// FIXME: Config params, move other to runtime
	unsigned	error_delay;
#	ifdef WITH_RAWSINK
	char		*rawsink_name;
	mode_t		rawsink_mode;
	bool		rawsink_rm;
#	endif
	// end-of-fixme

	struct process_t	*proc;
	struct device_t		*dev;
	struct encoder_t	*encoder;
};


struct stream_t *stream_init(struct device_t *dev, struct encoder_t *encoder);
void stream_destroy(struct stream_t *stream);

void stream_loop(struct stream_t *stream);
void stream_loop_break(struct stream_t *stream);
void stream_switch_slowdown(struct stream_t *stream, bool slowdown);
