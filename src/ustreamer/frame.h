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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/tools.h"
#include "../common/logging.h"


struct frame_t {
	const char	*role;
	uint8_t		*data;
	size_t		used;
	size_t		allocated;
	unsigned	width;
	unsigned	height;
	long double	grab_ts;
	long double	encode_begin_ts;
	long double	encode_end_ts;
};


struct frame_t *frame_init(const char *role);
void frame_destroy(struct frame_t *frame);

void frame_realloc_data(struct frame_t *frame, size_t size);
void frame_set_data(struct frame_t *frame, const uint8_t *data, size_t size);
void frame_append_data(struct frame_t *frame, const uint8_t *data, size_t size);

void frame_copy(const struct frame_t *src, struct frame_t *dest);
bool frame_compare(const struct frame_t *a, const struct frame_t *b);
