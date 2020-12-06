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


#include "picture.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "../common/tools.h"
#include "../common/logging.h"


struct picture_t *picture_init(void) {
	struct picture_t *picture;

	A_CALLOC(picture, 1);
	return picture;
}

void picture_destroy(struct picture_t *picture) {
	if (picture->data) {
		free(picture->data);
	}
	free(picture);
}

size_t picture_get_generous_size(unsigned width, unsigned height) {
	return ((width * height) << 1) * 2;
}

void picture_realloc_data(struct picture_t *picture, size_t size) {
	if (picture->allocated < size) {
		LOG_DEBUG("Increasing picture %p buffer: %zu -> %zu (+%zu)",
			picture, picture->allocated, size, size - picture->allocated);
		A_REALLOC(picture->data, size);
		picture->allocated = size;
	}
}

void picture_set_data(struct picture_t *picture, const unsigned char *data, size_t size) {
	picture_realloc_data(picture, size);
	memcpy(picture->data, data, size);
	picture->used = size;
}

void picture_append_data(struct picture_t *picture, const unsigned char *data, size_t size) {
	size_t new_used = picture->used + size;

	picture_realloc_data(picture, new_used);
	memcpy(picture->data + picture->used, data, size);
	picture->used = new_used;
}

void picture_copy(const struct picture_t *src, struct picture_t *dest) {
	picture_set_data(dest, src->data, src->used);

#	define COPY(_field) dest->_field = src->_field

	COPY(used);

	COPY(width);
	COPY(height);

	COPY(grab_ts);
	COPY(encode_begin_ts);
	COPY(encode_end_ts);

#	undef COPY
}

bool picture_compare(const struct picture_t *a, const struct picture_t *b) {
	return (
		a->allocated && b->allocated
		&& a->used == b->used
		&& !memcmp(a->data, b->data, b->used)
	);
}
