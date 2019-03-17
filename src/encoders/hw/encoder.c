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


#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "../../tools.h"
#include "../../logging.h"
#include "../../xioctl.h"
#include "../../device.h"

#include "huffman.h"
#include "encoder.h"


static bool _is_huffman(const unsigned char *data);
static size_t _memcpy_with_huffman(unsigned char *dest, const unsigned char *src, size_t size);


int hw_encoder_prepare_live(struct device_t *dev, unsigned quality) {
	struct v4l2_jpegcompression comp;

	MEMSET_ZERO(comp);

	if (xioctl(dev->run->fd, VIDIOC_G_JPEGCOMP, &comp) < 0) {
		LOG_ERROR("Can't query HW JPEG encoder params and set quality (unsupported)");
		return -1;
	}
	comp.quality = quality;
	if (xioctl(dev->run->fd, VIDIOC_S_JPEGCOMP, &comp) < 0) {
		LOG_ERROR("Can't set HW JPEG encoder quality (unsopported)");
		return -1;
	}
	return 0;
}

void hw_encoder_compress_buffer(struct device_t *dev, unsigned index) {
	if (dev->run->format != V4L2_PIX_FMT_MJPEG && dev->run->format != V4L2_PIX_FMT_JPEG) {
		assert(0 && "Unsupported input format for HW JPEG encoder");
	}

#	define PICTURE(_next)	dev->run->pictures[index]._next
#	define HW_BUFFER(_next)	dev->run->hw_buffers[index]._next

	assert(PICTURE(allocated) >= HW_BUFFER(used) + sizeof(HUFFMAN_TABLE));
	PICTURE(used) = _memcpy_with_huffman(PICTURE(data), HW_BUFFER(data), HW_BUFFER(used));

#	undef HW_BUFFER
#	undef PICTURE
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

static size_t _memcpy_with_huffman(unsigned char *dest, const unsigned char *src, size_t size) {
	if (!_is_huffman(src)) {
		const unsigned char *src_ptr = src;
		const unsigned char *src_end = src + size;
		size_t paste;

		while ((((src_ptr[0] << 8) | src_ptr[1]) != 0xFFC0) && (src_ptr < src_end)) {
			src_ptr += 1;
		}
		if (src_ptr >= src_end) {
			return 0;
		}
		paste = src_ptr - src;

		memcpy(dest, src, paste);
		memcpy(dest + paste, HUFFMAN_TABLE, sizeof(HUFFMAN_TABLE));
		memcpy(dest + paste + sizeof(HUFFMAN_TABLE), src_ptr, size - paste);
		return (size + sizeof(HUFFMAN_TABLE));
	} else {
		memcpy(dest, src, size);
		return size;
	}
}
