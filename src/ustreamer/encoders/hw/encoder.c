/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    This source file based on code of MJPG-Streamer.                        #
#                                                                            #
#    Copyright (C) 2005-2006  Laurent Pinchart & Michel Xhaard               #
#    Copyright (C) 2006  Gabriel A. Devenyi                                  #
#    Copyright (C) 2007  Tom Stöveken                                        #
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


#include "encoder.h"


void _copy_plus_huffman(const struct hw_buffer_t *src, struct picture_t *dest);
static bool _is_huffman(const unsigned char *data);


int hw_encoder_prepare(struct device_t *dev, unsigned quality) {
	struct v4l2_jpegcompression comp;

	MEMSET_ZERO(comp);

	if (xioctl(dev->run->fd, VIDIOC_G_JPEGCOMP, &comp) < 0) {
		LOG_ERROR("Device does not support setting of HW encoding quality parameters");
		return -1;
	}
	comp.quality = quality;
	if (xioctl(dev->run->fd, VIDIOC_S_JPEGCOMP, &comp) < 0) {
		LOG_ERROR("Unable to change MJPG quality for JPEG source with HW pass-through encoder");
		return -1;
	}
	return 0;
}

void hw_encoder_compress_buffer(struct hw_buffer_t *hw, struct picture_t *picture) {
	if (hw->format != V4L2_PIX_FMT_MJPEG && hw->format != V4L2_PIX_FMT_JPEG) {
		assert(0 && "Unsupported input format for HW encoder");
	}
	_copy_plus_huffman(hw, picture);
}

void _copy_plus_huffman(const struct hw_buffer_t *src, struct picture_t *dest) {
	if (!_is_huffman(src->data)) {
		const unsigned char *src_ptr = src->data;
		const unsigned char *src_end = src->data + src->used;
		size_t paste;

		while ((((src_ptr[0] << 8) | src_ptr[1]) != 0xFFC0) && (src_ptr < src_end)) {
			src_ptr += 1;
		}
		if (src_ptr >= src_end) {
			dest->used = 0; // Error
			return;
		}
		paste = src_ptr - src->data;

		picture_set_data(dest, src->data, paste);
		picture_append_data(dest, HUFFMAN_TABLE, sizeof(HUFFMAN_TABLE));
		picture_append_data(dest, src_ptr, src->used - paste);
	} else {
		picture_set_data(dest, src->data, src->used);
	}
}

static bool _is_huffman(const unsigned char *data) {
	unsigned count = 0;

	while (((data[0] << 8) | data[1]) != 0xFFDA) {
		if (count++ > 2048) {
			return false;
		}
		if (((data[0] << 8) | data[1]) == 0xFFC4) {
			return true;
		}
		data += 1;
	}
	return false;
}
