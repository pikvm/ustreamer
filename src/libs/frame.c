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


#include "frame.h"


frame_s *frame_init(const char *name) {
	frame_s *frame;
	A_CALLOC(frame, 1);
	frame->name = name;
	frame->managed = true;
	frame_realloc_data(frame, 512 * 1024);
	return frame;
}

void frame_destroy(frame_s *frame) {
	assert(frame->managed);
	if (frame->data) {
		free(frame->data);
	}
	free(frame);
}

void frame_realloc_data(frame_s *frame, size_t size) {
	assert(frame->managed);
	if (frame->allocated < size) {
		LOG_DEBUG("Increasing frame buffer '%s': %zu -> %zu (+%zu)",
			frame->name, frame->allocated, size, size - frame->allocated);
		A_REALLOC(frame->data, size);
		frame->allocated = size;
	}
}

void frame_set_data(frame_s *frame, const uint8_t *data, size_t size) {
	assert(frame->managed);
	frame_realloc_data(frame, size);
	memcpy(frame->data, data, size);
	frame->used = size;
}

void frame_append_data(frame_s *frame, const uint8_t *data, size_t size) {
	assert(frame->managed);
	size_t new_used = frame->used + size;
	frame_realloc_data(frame, new_used);
	memcpy(frame->data + frame->used, data, size);
	frame->used = new_used;
}

#define COPY(_field) dest->_field = src->_field

void frame_copy(const frame_s *src, frame_s *dest) {
	assert(dest->managed);
	frame_set_data(dest, src->data, src->used);
	COPY(used);
	frame_copy_meta(src, dest);
}

void frame_copy_meta(const frame_s *src, frame_s *dest) {
	// Don't copy the name
	COPY(width);
	COPY(height);
	COPY(format);
	COPY(stride);
	COPY(online);
	COPY(key);
	COPY(grab_ts);
	COPY(encode_begin_ts);
	COPY(encode_end_ts);
}

#undef COPY

bool frame_compare(const frame_s *a, const frame_s *b) {
#	define CMP(_field) (a->_field == b->_field)
	return (
		a->allocated && b->allocated
		&& CMP(used)
		&& CMP(width)
		&& CMP(height)
		&& CMP(format)
		&& CMP(stride)
		&& CMP(online)
		&& CMP(key)
		&& !memcmp(a->data, b->data, b->used)
	);
#	undef CMP
}

unsigned frame_get_padding(const frame_s *frame) {
	unsigned bytes_per_pixel = 0;
	switch (frame->format) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_RGB565: bytes_per_pixel = 2; break;
		case V4L2_PIX_FMT_RGB24: bytes_per_pixel = 3; break;
		// case V4L2_PIX_FMT_H264:
		case V4L2_PIX_FMT_MJPEG:
		case V4L2_PIX_FMT_JPEG: bytes_per_pixel = 0; break;
		default: assert(0 && "Unknown pixelformat");
	}
	if (bytes_per_pixel > 0 && frame->stride > frame->width) {
		return (frame->stride - frame->width * bytes_per_pixel);
	}
	return 0;
}

const char *fourcc_to_string(unsigned format, char *buf, size_t size) {
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
