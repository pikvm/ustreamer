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


#include "frame.h"


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

void us_frame_realloc_data(us_frame_s *frame, size_t size) {
	if (frame->allocated < size) {
		US_REALLOC(frame->data, size);
		frame->allocated = size;
	}
}

void us_frame_set_data(us_frame_s *frame, const uint8_t *data, size_t size) {
	us_frame_realloc_data(frame, size);
	memcpy(frame->data, data, size);
	frame->used = size;
}

void us_frame_append_data(us_frame_s *frame, const uint8_t *data, size_t size) {
	const size_t new_used = frame->used + size;
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
		&& US_FRAME_COMPARE_META_USED_NOTS(a, b)
		&& !memcmp(a->data, b->data, b->used)
	);
}

unsigned us_frame_get_padding(const us_frame_s *frame) {
	unsigned bytes_per_pixel = 0;
	switch (frame->format) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_RGB565: bytes_per_pixel = 2; break;
		case V4L2_PIX_FMT_BGR24:
		case V4L2_PIX_FMT_RGB24: bytes_per_pixel = 3; break;
		// case V4L2_PIX_FMT_H264:
		case V4L2_PIX_FMT_MJPEG:
		case V4L2_PIX_FMT_JPEG: bytes_per_pixel = 0; break;
		default: assert(0 && "Unknown format");
	}
	if (bytes_per_pixel > 0 && frame->stride > frame->width) {
		return (frame->stride - frame->width * bytes_per_pixel);
	}
	return 0;
}

const char *us_fourcc_to_string(unsigned format, char *buf, size_t size) {
	assert(size >= 8);
	buf[0] = format & 0x7F;
	buf[1] = (format >> 8) & 0x7F;
	buf[2] = (format >> 16) & 0x7F;
	buf[3] = (format >> 24) & 0x7F;
	if (format & ((unsigned)1 << 31)) {
		buf[4] = '-';
		buf[5] = 'B';
		buf[6] = 'E';
		buf[7] = '\0';
	} else {
		buf[4] = '\0';
	}
	return buf;
}
