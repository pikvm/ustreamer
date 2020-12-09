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


struct frame_t *frame_init(const char *role) {
	struct frame_t *frame;

	A_CALLOC(frame, 1);
	frame->role = role;
	frame_realloc_data(frame, 500 * 1024);
	return frame;
}

void frame_destroy(struct frame_t *frame) {
	if (frame->data) {
		free(frame->data);
	}
	free(frame);
}

void frame_realloc_data(struct frame_t *frame, size_t size) {
	if (frame->allocated < size) {
		LOG_DEBUG("Increasing frame buffer '%s': %zu -> %zu (+%zu)",
			frame->role, frame->allocated, size, size - frame->allocated);
		A_REALLOC(frame->data, size);
		frame->allocated = size;
	}
}

void frame_set_data(struct frame_t *frame, const unsigned char *data, size_t size) {
	frame_realloc_data(frame, size);
	memcpy(frame->data, data, size);
	frame->used = size;
}

void frame_append_data(struct frame_t *frame, const unsigned char *data, size_t size) {
	size_t new_used = frame->used + size;

	frame_realloc_data(frame, new_used);
	memcpy(frame->data + frame->used, data, size);
	frame->used = new_used;
}

void frame_copy(const struct frame_t *src, struct frame_t *dest) {
	frame_set_data(dest, src->data, src->used);

#	define COPY(_field) dest->_field = src->_field

	// Don't copy the role
	COPY(used);

	COPY(width);
	COPY(height);

	COPY(grab_ts);
	COPY(encode_begin_ts);
	COPY(encode_end_ts);

#	undef COPY
}

bool frame_compare(const struct frame_t *a, const struct frame_t *b) {
	return (
		a->allocated && b->allocated
		&& a->used == b->used
		&& !memcmp(a->data, b->data, b->used)
	);
}
