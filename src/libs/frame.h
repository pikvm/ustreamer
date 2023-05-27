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
	uint8_t		*data;
	size_t		used;
	size_t		allocated;
	int			dma_fd;

	unsigned	width;
	unsigned	height;
	unsigned	format;
	unsigned	stride;
	// Stride is a bytesperline in V4L2
	// https://www.kernel.org/doc/html/v4.14/media/uapi/v4l/pixfmt-v4l2.html
	// https://medium.com/@oleg.shipitko/what-does-stride-mean-in-image-processing-bba158a72bcd

	bool		online;
	bool		key;
	unsigned	gop;

	long double	grab_ts;
	long double	encode_begin_ts;
	long double	encode_end_ts;
} us_frame_s;


#define US_FRAME_COPY_META(x_src, x_dest) { \
		x_dest->width = x_src->width; \
		x_dest->height = x_src->height; \
		x_dest->format = x_src->format; \
		x_dest->stride = x_src->stride; \
		x_dest->online = x_src->online; \
		x_dest->key = x_src->key; \
		x_dest->gop = x_src->gop; \
		x_dest->grab_ts = x_src->grab_ts; \
		x_dest->encode_begin_ts = x_src->encode_begin_ts; \
		x_dest->encode_end_ts = x_src->encode_end_ts; \
	}

static inline void us_frame_copy_meta(const us_frame_s *src, us_frame_s *dest) {
	US_FRAME_COPY_META(src, dest);
}

#define US_FRAME_COMPARE_META_USED_NOTS(x_a, x_b) ( \
		x_a->used == x_b->used \
		&& x_a->width == x_b->width \
		&& x_a->height == x_b->height \
		&& x_a->format == x_b->format \
		&& x_a->stride == x_b->stride \
		&& x_a->online == x_b->online \
		&& x_a->key == x_b->key \
		&& x_a->gop == x_b->gop \
	)


static inline void us_frame_encoding_begin(const us_frame_s *src, us_frame_s *dest, unsigned format) {
	assert(src->used > 0);
	us_frame_copy_meta(src, dest);
	dest->encode_begin_ts = us_get_now_monotonic();
	dest->format = format;
	dest->stride = 0;
	dest->used = 0;
}

static inline void us_frame_encoding_end(us_frame_s *dest) {
	assert(dest->used > 0);
	dest->encode_end_ts = us_get_now_monotonic();
}


us_frame_s *us_frame_init(void);
void us_frame_destroy(us_frame_s *frame);

void us_frame_realloc_data(us_frame_s *frame, size_t size);
void us_frame_set_data(us_frame_s *frame, const uint8_t *data, size_t size);
void us_frame_append_data(us_frame_s *frame, const uint8_t *data, size_t size);

void us_frame_copy(const us_frame_s *src, us_frame_s *dest);
bool us_frame_compare(const us_frame_s *a, const us_frame_s *b);

unsigned us_frame_get_padding(const us_frame_s *frame);

const char *us_fourcc_to_string(unsigned format, char *buf, size_t size);

static inline bool us_is_jpeg(unsigned format) {
	return (format == V4L2_PIX_FMT_JPEG || format == V4L2_PIX_FMT_MJPEG);
}
