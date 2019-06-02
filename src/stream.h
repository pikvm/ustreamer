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

#include <stdatomic.h>

#include "device.h"
#include "encoder.h"


struct process_t {
	atomic_bool stop;
	atomic_bool slowdown;
};

struct stream_t {
	struct picture_t	picture;
	unsigned			width;
	unsigned			height;
	unsigned			captured_fps;
	atomic_bool			updated;
	pthread_mutex_t		mutex;

	struct process_t	*proc;
	struct device_t		*dev;
	struct encoder_t	*encoder;
};


struct stream_t *stream_init(struct device_t *dev, struct encoder_t *encoder);
void stream_destroy(struct stream_t *stream);

void stream_loop(struct stream_t *stream);
void stream_loop_break(struct stream_t *stream);
void stream_switch_slowdown(struct stream_t *stream, bool slowdown);
