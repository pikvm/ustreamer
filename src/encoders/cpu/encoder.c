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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <jpeglib.h>
#include <linux/videodev2.h>

#include "../../tools.h"
#include "../../device.h"

#include "encoder.h"


struct _jpeg_dest_manager_t {
	struct			jpeg_destination_mgr mgr; // Default manager
	JOCTET			*buffer; // Start of buffer
	unsigned char	*outbuffer_cursor;
	size_t			*written;
};


static void _jpeg_set_dest_picture(j_compress_ptr jpeg, unsigned char *picture, size_t *written);

static void _jpeg_write_scanlines_yuyv(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height);

static void _jpeg_write_scanlines_uyvy(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height);

static void _jpeg_write_scanlines_rgb565(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height);

static void _jpeg_write_scanlines_rgb24(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height);

static void _jpeg_init_destination(j_compress_ptr jpeg);
static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg);
static void _jpeg_term_destination(j_compress_ptr jpeg);


void cpu_encoder_compress_buffer(struct device_t *dev, unsigned index, unsigned quality) {
	// This function based on compress_image_to_jpeg() from mjpg-streamer

	struct jpeg_compress_struct jpeg;
	struct jpeg_error_mgr jpeg_error;

	jpeg.err = jpeg_std_error(&jpeg_error);
	jpeg_create_compress(&jpeg);

#	define PICTURE(_next) dev->run->pictures[index]._next

	PICTURE(size) = 0;
	_jpeg_set_dest_picture(&jpeg, PICTURE(data), &PICTURE(size));

	jpeg.image_width = dev->run->width;
	jpeg.image_height = dev->run->height;
	jpeg.input_components = 3;
	jpeg.in_color_space = JCS_RGB;

	jpeg_set_defaults(&jpeg);
	jpeg_set_quality(&jpeg, quality, TRUE);

	jpeg_start_compress(&jpeg, TRUE);

#	define WRITE_SCANLINES(_format, _func) \
		case _format: { _func(&jpeg, dev->run->hw_buffers[index].start, dev->run->width, dev->run->height); break; }

	switch (dev->run->format) {
		// https://www.fourcc.org/yuv.php
		WRITE_SCANLINES(V4L2_PIX_FMT_YUYV, _jpeg_write_scanlines_yuyv);
		WRITE_SCANLINES(V4L2_PIX_FMT_UYVY, _jpeg_write_scanlines_uyvy);
		WRITE_SCANLINES(V4L2_PIX_FMT_RGB565, _jpeg_write_scanlines_rgb565);
		WRITE_SCANLINES(V4L2_PIX_FMT_RGB24, _jpeg_write_scanlines_rgb24);
		default: assert(0 && "Unsupported input format for CPU JPEG encoder");
	}

#	undef WRITE_SCANLINES

	// TODO: process jpeg errors:
	// https://stackoverflow.com/questions/19857766/error-handling-in-libjpeg
	jpeg_finish_compress(&jpeg);
	jpeg_destroy_compress(&jpeg);
	assert(PICTURE(size) > 0);
	assert(PICTURE(size) <= dev->run->max_picture_size);
}

static void _jpeg_set_dest_picture(j_compress_ptr jpeg, unsigned char *picture, size_t *written) {
	struct _jpeg_dest_manager_t *dest;

	if (jpeg->dest == NULL) {
		assert((jpeg->dest = (struct jpeg_destination_mgr *)(*jpeg->mem->alloc_small)(
			(j_common_ptr) jpeg, JPOOL_PERMANENT, sizeof(struct _jpeg_dest_manager_t)
		)));
	}

	dest = (struct _jpeg_dest_manager_t *)jpeg->dest;
	dest->mgr.init_destination = _jpeg_init_destination;
	dest->mgr.empty_output_buffer = _jpeg_empty_output_buffer;
	dest->mgr.term_destination = _jpeg_term_destination;
	dest->outbuffer_cursor = picture;
	dest->written = written;
}

#define NORM_COMPONENT(_x) (((_x) > 255) ? 255 : (((_x) < 0) ? 0 : (_x)))

static void _jpeg_write_scanlines_yuyv(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height) {

	unsigned char *line_buffer;
	JSAMPROW scanlines[1];
	unsigned z = 0;

	A_CALLOC(line_buffer, width * 3);

	while (jpeg->next_scanline < height) {
		unsigned char *ptr = line_buffer;

		for (unsigned x = 0; x < width; ++x) {
			int y = (!z ? data[0] << 8 : data[2] << 8);
			int u = data[1] - 128;
			int v = data[3] - 128;

			int r = (y + (359 * v)) >> 8;
			int g = (y - (88 * u) - (183 * v)) >> 8;
			int b = (y + (454 * u)) >> 8;

			*(ptr++) = NORM_COMPONENT(r);
			*(ptr++) = NORM_COMPONENT(g);
			*(ptr++) = NORM_COMPONENT(b);

			if (z++) {
				z = 0;
				data += 4;
			}
		}

		scanlines[0] = line_buffer;
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buffer);
}

static void _jpeg_write_scanlines_uyvy(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height) {

	unsigned char *line_buffer;
	JSAMPROW scanlines[1];
	unsigned z = 0;

	A_CALLOC(line_buffer, width * 3);

	while (jpeg->next_scanline < height) {
		unsigned char *ptr = line_buffer;

		for(unsigned x = 0; x < width; ++x) {
			int y = (!z ? data[1] << 8 : data[3] << 8);
			int u = data[0] - 128;
			int v = data[2] - 128;

			int r = (y + (359 * v)) >> 8;
			int g = (y - (88 * u) - (183 * v)) >> 8;
			int b = (y + (454 * u)) >> 8;

			*(ptr++) = NORM_COMPONENT(r);
			*(ptr++) = NORM_COMPONENT(g);
			*(ptr++) = NORM_COMPONENT(b);

			if (z++) {
				z = 0;
				data += 4;
			}
		}

		scanlines[0] = line_buffer;
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buffer);
}

#undef NORM_COMPONENT

static void _jpeg_write_scanlines_rgb565(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height) {

	unsigned char *line_buffer;
	JSAMPROW scanlines[1];

	A_CALLOC(line_buffer, width * 3);

	while (jpeg->next_scanline < height) {
		unsigned char *ptr = line_buffer;

		for(unsigned x = 0; x < width; ++x) {
			unsigned int two_byte = (data[1] << 8) + data[0];

			*(ptr++) = data[1] & 248;
			*(ptr++) = (unsigned char)((two_byte & 2016) >> 3);
			*(ptr++) = (data[0] & 31) * 8;

			data += 2;
		}

		scanlines[0] = line_buffer;
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buffer);
}

static void _jpeg_write_scanlines_rgb24(
	struct jpeg_compress_struct *jpeg, const unsigned char *data,
	unsigned width, unsigned height) {

	JSAMPROW scanlines[1];

	while (jpeg->next_scanline < height) {
		scanlines[0] = (unsigned char *)(data + jpeg->next_scanline * width * 3);
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}
}

#define JPEG_OUTPUT_BUFFER_SIZE  4096

static void _jpeg_init_destination(j_compress_ptr jpeg) {
	struct _jpeg_dest_manager_t *dest = (struct _jpeg_dest_manager_t *)jpeg->dest;

	// Allocate the output buffer - it will be released when done with image
	assert((dest->buffer = (JOCTET *)(*jpeg->mem->alloc_small)(
		(j_common_ptr) jpeg, JPOOL_IMAGE, JPEG_OUTPUT_BUFFER_SIZE * sizeof(JOCTET)
	)));

	dest->mgr.next_output_byte = dest->buffer;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;
}

static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg) {
	// Called whenever local jpeg buffer fills up

	struct _jpeg_dest_manager_t *dest = (struct _jpeg_dest_manager_t *)jpeg->dest;

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

	struct _jpeg_dest_manager_t *dest = (struct _jpeg_dest_manager_t *)jpeg->dest;
	size_t data_count = JPEG_OUTPUT_BUFFER_SIZE - dest->mgr.free_in_buffer;

	// Write any data remaining in the buffer
	memcpy(dest->outbuffer_cursor, dest->buffer, data_count);
	dest->outbuffer_cursor += data_count;
	*dest->written += data_count;
}

#undef JPEG_OUTPUT_BUFFER_SIZE
