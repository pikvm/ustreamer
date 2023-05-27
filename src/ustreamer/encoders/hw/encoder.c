/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file based on code of MJPG-Streamer.                        #
#                                                                            #
#    Copyright (C) 2005-2006  Laurent Pinchart & Michel Xhaard               #
#    Copyright (C) 2006  Gabriel A. Devenyi                                  #
#    Copyright (C) 2007  Tom Stöveken                                        #
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


#include "encoder.h"


void _copy_plus_huffman(const us_frame_s *src, us_frame_s *dest);
static bool _is_huffman(const uint8_t *data);


void us_hw_encoder_compress(const us_frame_s *src, us_frame_s *dest) {
	assert(us_is_jpeg(src->format));
	_copy_plus_huffman(src, dest);
}

void _copy_plus_huffman(const us_frame_s *src, us_frame_s *dest) {
	us_frame_encoding_begin(src, dest, V4L2_PIX_FMT_JPEG);

	if (!_is_huffman(src->data)) {
		const uint8_t *src_ptr = src->data;
		const uint8_t *const src_end = src->data + src->used;

		while ((((src_ptr[0] << 8) | src_ptr[1]) != 0xFFC0) && (src_ptr < src_end)) {
			src_ptr += 1;
		}
		if (src_ptr >= src_end) {
			dest->used = 0; // Error
			return;
		}

		const size_t paste = src_ptr - src->data;

		us_frame_set_data(dest, src->data, paste);
		us_frame_append_data(dest, US_HUFFMAN_TABLE, sizeof(US_HUFFMAN_TABLE));
		us_frame_append_data(dest, src_ptr, src->used - paste);

	} else {
		us_frame_set_data(dest, src->data, src->used);
	}

	us_frame_encoding_end(dest);
}

static bool _is_huffman(const uint8_t *data) {
	unsigned count = 0;

	while ((((uint16_t)data[0] << 8) | data[1]) != 0xFFDA) {
		if (count++ > 2048) {
			return false;
		}
		if ((((uint16_t)data[0] << 8) | data[1]) == 0xFFC4) {
			return true;
		}
		data += 1;
	}
	return false;
}
