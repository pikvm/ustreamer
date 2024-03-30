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


#pragma once

#include "types.h"
#include "tools.h"


#define US_FRAME_META_DECLARE \
	uint	width; \
	uint	height; \
	uint	format; \
	uint	stride; \
	/* Stride is a bytesperline in V4L2 */ \
	/* https://www.kernel.org/doc/html/v4.14/media/uapi/v4l/pixfmt-v4l2.html */ \
	/* https://medium.com/@oleg.shipitko/what-does-stride-mean-in-image-processing-bba158a72bcd */ \
	bool	online; \
	bool	key; \
	uint	gop; \
	\
	ldf		grab_ts; \
	ldf		encode_begin_ts; \
	ldf		encode_end_ts;


typedef struct {
	u8		*data;
	uz		used;
	uz		allocated;
	int		dma_fd;

	US_FRAME_META_DECLARE;
} us_frame_s;


#define US_FRAME_COPY_META(x_src, x_dest) { \
		(x_dest)->width = (x_src)->width; \
		(x_dest)->height = (x_src)->height; \
		(x_dest)->format = (x_src)->format; \
		(x_dest)->stride = (x_src)->stride; \
		(x_dest)->online = (x_src)->online; \
		(x_dest)->key = (x_src)->key; \
		(x_dest)->gop = (x_src)->gop; \
		\
		(x_dest)->grab_ts = (x_src)->grab_ts; \
		(x_dest)->encode_begin_ts = (x_src)->encode_begin_ts; \
		(x_dest)->encode_end_ts = (x_src)->encode_end_ts; \
	}

#define US_FRAME_COMPARE_GEOMETRY(x_a, x_b) ( \
		/* Compare the used size and significant meta (no timings) */ \
		(x_a)->used == (x_b)->used \
		\
		&& (x_a)->width == (x_b)->width \
		&& (x_a)->height == (x_b)->height \
		&& (x_a)->format == (x_b)->format \
		&& (x_a)->stride == (x_b)->stride \
		&& (x_a)->online == (x_b)->online \
		&& (x_a)->key == (x_b)->key \
		&& (x_a)->gop == (x_b)->gop \
	)


static inline void us_frame_encoding_begin(const us_frame_s *src, us_frame_s *dest, uint format) {
	assert(src->used > 0);
	US_FRAME_COPY_META(src, dest);
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

void us_frame_realloc_data(us_frame_s *frame, uz size);
void us_frame_set_data(us_frame_s *frame, const u8 *data, uz size);
void us_frame_append_data(us_frame_s *frame, const u8 *data, uz size);

void us_frame_copy(const us_frame_s *src, us_frame_s *dest);
bool us_frame_compare(const us_frame_s *a, const us_frame_s *b);

uint us_frame_get_padding(const us_frame_s *frame);

bool us_is_jpeg(uint format);
const char *us_fourcc_to_string(uint format, char *buf, uz size);
