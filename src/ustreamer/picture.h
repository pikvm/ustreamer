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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/tools.h"
#include "../common/logging.h"


struct picture_t {
	unsigned char	*data;
	size_t			used;
	size_t			allocated;
	unsigned		width;
	unsigned		height;
	long double		grab_ts;
	long double		encode_begin_ts;
	long double		encode_end_ts;
};


struct picture_t *picture_init(void);
void picture_destroy(struct picture_t *picture);

size_t picture_get_generous_size(unsigned width, unsigned height);

void picture_realloc_data(struct picture_t *picture, size_t size);
void picture_set_data(struct picture_t *picture, const unsigned char *data, size_t size);
void picture_append_data(struct picture_t *picture, const unsigned char *data, size_t size);

void picture_copy(const struct picture_t *src, struct picture_t *dest);
bool picture_compare(const struct picture_t *a, const struct picture_t *b);
