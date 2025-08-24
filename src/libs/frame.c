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


#include "frame.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "types.h"
#include "tools.h"


us_frame_s *us_frame_init(void) {
	us_frame_s *frame;
	US_CALLOC(frame, 1);
	us_frame_realloc_data(frame, 512 * 1024);
	frame->dma_fd = -1;
	return frame;
}

void us_frame_destroy(us_frame_s *frame) {
	US_DELETE(frame->data, free);
	free(frame);
}

void us_frame_realloc_data(us_frame_s *frame, uz size) {
	if (frame->allocated < size) {
		US_REALLOC(frame->data, size);
		frame->allocated = size;
	}
}

void us_frame_set_data(us_frame_s *frame, const u8 *data, uz size) {
	us_frame_realloc_data(frame, size);
	memcpy(frame->data, data, size);
	frame->used = size;
}

void us_frame_append_data(us_frame_s *frame, const u8 *data, uz size) {
	const uz new_used = frame->used + size;
	us_frame_realloc_data(frame, new_used);
	memcpy(frame->data + frame->used, data, size);
	frame->used = new_used;
}

void us_frame_copy(const us_frame_s *src, us_frame_s *dest) {
	us_frame_set_data(dest, src->data, src->used);
	US_FRAME_COPY_META(src, dest);
}

bool us_frame_compare(const us_frame_s *a, const us_frame_s *b) {
	return (
		a->allocated && b->allocated
		&& US_FRAME_COMPARE_GEOMETRY(a, b)
		&& !memcmp(a->data, b->data, b->used)
	);
}

uint us_frame_get_padding(const us_frame_s *frame) {
	uint bytes_per_pixel = 0;
	switch (frame->format) {
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
		case V4L2_PIX_FMT_GREY:
			bytes_per_pixel = 1;
			break;

		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV24:
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_RGB565:
			bytes_per_pixel = 2;
			break;

		case V4L2_PIX_FMT_BGR24:
		case V4L2_PIX_FMT_RGB24:
			bytes_per_pixel = 3;
			break;

		// case V4L2_PIX_FMT_H264:
		case V4L2_PIX_FMT_MJPEG:
		case V4L2_PIX_FMT_JPEG:
			bytes_per_pixel = 0;
			break;

		default:
			assert(0 && "Unknown format");
	}
	if (bytes_per_pixel > 0 && frame->stride > frame->width) {
		return (frame->stride - frame->width * bytes_per_pixel);
	}
	return 0;
}

bool us_is_jpeg(uint format) {
	return (format == V4L2_PIX_FMT_JPEG || format == V4L2_PIX_FMT_MJPEG);
}

const char *us_fourcc_to_string(uint format, char *buf, uz size) {
	assert(size >= 8);
	buf[0] = format & 0x7F;
	buf[1] = (format >> 8) & 0x7F;
	buf[2] = (format >> 16) & 0x7F;
	buf[3] = (format >> 24) & 0x7F;
	if (format & ((uint)1 << 31)) {
		buf[4] = '-';
		buf[5] = 'B';
		buf[6] = 'E';
		buf[7] = '\0';
	} else {
		buf[4] = '\0';
	}
	return buf;
}
