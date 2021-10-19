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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "tools.h"


typedef struct {
	uint8_t	*data;
	size_t	used;
	size_t	allocated;

	unsigned width;
	unsigned height;
	unsigned format;
	unsigned stride;
	// Stride is a bytesperline in V4L2
	// https://www.kernel.org/doc/html/v4.14/media/uapi/v4l/pixfmt-v4l2.html
	// https://medium.com/@oleg.shipitko/what-does-stride-mean-in-image-processing-bba158a72bcd

	bool online;
	bool key;

	long double	grab_ts;
	long double	encode_begin_ts;
	long double	encode_end_ts;
} frame_s;


#define FRAME_COPY_META(_src, _dest) { \
		_dest->width = _src->width; \
		_dest->height = _src->height; \
		_dest->format = _src->format; \
		_dest->stride = _src->stride; \
		_dest->online = _src->online; \
		_dest->key = _src->key; \
		_dest->grab_ts = _src->grab_ts; \
		_dest->encode_begin_ts = _src->encode_begin_ts; \
		_dest->encode_end_ts = _src->encode_end_ts; \
	}

static inline void frame_copy_meta(const frame_s *src, frame_s *dest) {
	FRAME_COPY_META(src, dest);
}

#define FRAME_COMPARE_META_USED_NOTS(_a, _b) ( \
		_a->used == _b->used \
		&& _a->width == _b->width \
		&& _a->height == _b->height \
		&& _a->format == _b->format \
		&& _a->stride == _b->stride \
		&& _a->online == _b->online \
		&& _a->key == _b->key \
	)


frame_s *frame_init(void);
void frame_destroy(frame_s *frame);

void frame_realloc_data(frame_s *frame, size_t size);
void frame_set_data(frame_s *frame, const uint8_t *data, size_t size);
void frame_append_data(frame_s *frame, const uint8_t *data, size_t size);

void frame_copy(const frame_s *src, frame_s *dest);
bool frame_compare(const frame_s *a, const frame_s *b);

unsigned frame_get_padding(const frame_s *frame);

const char *fourcc_to_string(unsigned format, char *buf, size_t size);

static inline bool is_jpeg(unsigned format) {
	return (format == V4L2_PIX_FMT_JPEG || format == V4L2_PIX_FMT_MJPEG);
}
