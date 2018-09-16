/*******************************************************************************
# Based on source code of mjpg-streamer.                                       #
#                                                                              #
#   Orginally Copyright (C) 2005 2006  Laurent Pinchart && Michel Xhaard       #
#   Modifications Copyright (C) 2006   Gabriel A. Devenyi                      #
#   Modifications Copyright (C) 2007   Tom Stöveken                            #
#   Modifications Copyright (C) 2018   Maxim Devaev                            #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <jpeglib.h>
#include <linux/videodev2.h>

#include "tools.h"
#include "device.h"
#include "jpeg.h"


#define JPEG_OUTPUT_BUFFER_SIZE  4096


struct mjpg_destination_mgr {
	struct			jpeg_destination_mgr mgr; // Default manager
	JOCTET			*buffer; // Start of buffer
	unsigned char	*outbuffer_cursor;
	unsigned long	*written;
};


static void _jpeg_set_dest_picture(j_compress_ptr jpeg, unsigned char *picture, unsigned long *written);

static void _jpeg_write_scanlines_yuyv(struct jpeg_compress_struct *jpeg,
    unsigned char *line_buffer, const unsigned char *data,
    const unsigned width, const unsigned height);

static void _jpeg_write_scanlines_uyvy(struct jpeg_compress_struct *jpeg,
	unsigned char *line_buffer, const unsigned char *data,
	const unsigned width, const unsigned height);

static void _jpeg_write_scanlines_rgb565(struct jpeg_compress_struct *jpeg,
	unsigned char *line_buffer, const unsigned char *data,
	const unsigned width, const unsigned height);

static void _jpeg_init_destination(j_compress_ptr jpeg);
static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg);
static void _jpeg_term_destination(j_compress_ptr jpeg);


unsigned long jpeg_compress_buffer(struct device_t *dev, int index) {
	// This function based on compress_image_to_jpeg() from mjpg-streamer

	struct jpeg_compress_struct jpeg;
	struct jpeg_error_mgr jpeg_error;
	unsigned char *line_buffer;

	A_CALLOC(line_buffer, dev->run->width * 3, sizeof(unsigned char));

	jpeg.err = jpeg_std_error(&jpeg_error);
	jpeg_create_compress(&jpeg);

	dev->run->pictures[index].size = 0;
	_jpeg_set_dest_picture(&jpeg, dev->run->pictures[index].data, &dev->run->pictures[index].size);

	jpeg.image_width = dev->run->width;
	jpeg.image_height = dev->run->height;
	jpeg.input_components = 3;
	jpeg.in_color_space = JCS_RGB;

	jpeg_set_defaults(&jpeg);
	jpeg_set_quality(&jpeg, dev->jpeg_quality, TRUE);

	jpeg_start_compress(&jpeg, TRUE);

#	define WRITE_SCANLINES(_func) \
		_func(&jpeg, line_buffer, dev->run->hw_buffers[index].start, dev->run->width, dev->run->height)

	switch (dev->run->format) {
		case V4L2_PIX_FMT_YUYV: WRITE_SCANLINES(_jpeg_write_scanlines_yuyv); break;
		case V4L2_PIX_FMT_UYVY: WRITE_SCANLINES(_jpeg_write_scanlines_uyvy); break;
		case V4L2_PIX_FMT_RGB565: WRITE_SCANLINES(_jpeg_write_scanlines_rgb565); break;
		default: assert(0 && "Unsupported input format for JPEG compressor");
	}

	// TODO: process jpeg errors:
	// https://stackoverflow.com/questions/19857766/error-handling-in-libjpeg
	jpeg_finish_compress(&jpeg);
	jpeg_destroy_compress(&jpeg);
	free(line_buffer);
	assert(dev->run->pictures[index].size > 0);
	return dev->run->pictures[index].size;
}

static void _jpeg_set_dest_picture(j_compress_ptr jpeg, unsigned char *picture, unsigned long *written) {
	struct mjpg_destination_mgr *dest;

	if (jpeg->dest == NULL) {
		assert((jpeg->dest = (struct jpeg_destination_mgr *)(*jpeg->mem->alloc_small)(
			(j_common_ptr) jpeg, JPOOL_PERMANENT, sizeof(struct mjpg_destination_mgr)
		)));
	}

	dest = (struct mjpg_destination_mgr *) jpeg->dest;
	dest->mgr.init_destination = _jpeg_init_destination;
	dest->mgr.empty_output_buffer = _jpeg_empty_output_buffer;
	dest->mgr.term_destination = _jpeg_term_destination;
	dest->outbuffer_cursor = picture;
	dest->written = written;
}

static void _jpeg_write_scanlines_yuyv(struct jpeg_compress_struct *jpeg,
	unsigned char *line_buffer, const unsigned char *data,
	const unsigned width, const unsigned height) {

	JSAMPROW scanlines[1];
	unsigned z = 0;

	while (jpeg->next_scanline < height) {
		unsigned char *ptr = line_buffer;

		for (unsigned x = 0; x < width; x++) {
			int y = (!z ? data[0] << 8 : data[2] << 8);
			int u = data[1] - 128;
			int v = data[3] - 128;

			int r = (y + (359 * v)) >> 8;
			int g = (y - (88 * u) - (183 * v)) >> 8;
			int b = (y + (454 * u)) >> 8;

			*(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
			*(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
			*(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

			if (z++) {
				z = 0;
				data += 4;
			}
		}

		scanlines[0] = line_buffer;
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}
}

static void _jpeg_write_scanlines_uyvy(struct jpeg_compress_struct *jpeg,
	unsigned char *line_buffer, const unsigned char *data,
	const unsigned width, const unsigned height) {

	JSAMPROW scanlines[1];
	unsigned z = 0;

	while(jpeg->next_scanline < height) {
		unsigned char *ptr = line_buffer;

		for(unsigned x = 0; x < width; x++) {
			int y = (!z ? data[1] << 8 : data[3] << 8);
			int u = data[0] - 128;
			int v = data[2] - 128;

			int r = (y + (359 * v)) >> 8;
			int g = (y - (88 * u) - (183 * v)) >> 8;
			int b = (y + (454 * u)) >> 8;

			*(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
			*(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
			*(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

			if (z++) {
				z = 0;
				data += 4;
			}
		}

		scanlines[0] = line_buffer;
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}
}

static void _jpeg_write_scanlines_rgb565(struct jpeg_compress_struct *jpeg,
	unsigned char *line_buffer, const unsigned char *data,
	const unsigned width, const unsigned height) {

	JSAMPROW scanlines[1];

	while(jpeg->next_scanline < height) {
		unsigned char *ptr = line_buffer;

		for(unsigned x = 0; x < width; x++) {
			unsigned int two_byte = (data[1] << 8) + data[0];

			*(ptr++) = data[1] & 248;
			*(ptr++) = (unsigned char)((two_byte & 2016) >> 3);
			*(ptr++) = (data[0] & 31) * 8;

			data += 2;
		}

		scanlines[0] = line_buffer;
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}
}

static void _jpeg_init_destination(j_compress_ptr jpeg) {
	struct mjpg_destination_mgr *dest = (struct mjpg_destination_mgr *) jpeg->dest;

	// Allocate the output buffer - it will be released when done with image
	assert((dest->buffer = (JOCTET *)(*jpeg->mem->alloc_small)(
		(j_common_ptr) jpeg, JPOOL_IMAGE, JPEG_OUTPUT_BUFFER_SIZE * sizeof(JOCTET)
	)));

	dest->mgr.next_output_byte = dest->buffer;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;
}

static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg) {
	// Called whenever local jpeg buffer fills up

	struct mjpg_destination_mgr *dest = (struct mjpg_destination_mgr *) jpeg->dest;

	memcpy(dest->outbuffer_cursor, dest->buffer, JPEG_OUTPUT_BUFFER_SIZE);
	dest->outbuffer_cursor += JPEG_OUTPUT_BUFFER_SIZE;
	*dest->written += JPEG_OUTPUT_BUFFER_SIZE;

	dest->mgr.next_output_byte = dest->buffer;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;

	return TRUE;
}

static void _jpeg_term_destination(j_compress_ptr jpeg) {
	// Called by jpeg_finish_compress after all data has been written.
	// Usually needs to flush buffer

	struct mjpg_destination_mgr *dest = (struct mjpg_destination_mgr *) jpeg->dest;
	size_t data_count = JPEG_OUTPUT_BUFFER_SIZE - dest->mgr.free_in_buffer;

	// Write any data remaining in the buffer
	memcpy(dest->outbuffer_cursor, dest->buffer, data_count);
	dest->outbuffer_cursor += data_count;
	*dest->written += data_count;
}
