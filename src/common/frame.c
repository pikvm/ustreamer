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


#include "frame.h"


frame_s *frame_init(const char *role) {
	frame_s *frame;

	A_CALLOC(frame, 1);
	frame->role = role;
	frame->managed = true;
	frame_realloc_data(frame, 500 * 1024);
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
			frame->role, frame->allocated, size, size - frame->allocated);
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
	// Don't copy the role
	COPY(width);
	COPY(height);
	COPY(format);
	COPY(online);
	COPY(grab_ts);
	COPY(encode_begin_ts);
	COPY(encode_end_ts);
}

#undef COPY

bool frame_compare(const frame_s *a, const frame_s *b) {
	return (
		a->allocated && b->allocated
		&& a->used == b->used
		&& !memcmp(a->data, b->data, b->used)
	);
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
