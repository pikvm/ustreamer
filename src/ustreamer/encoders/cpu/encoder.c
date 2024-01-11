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


typedef struct {
	struct	jpeg_destination_mgr mgr; // Default manager
	JOCTET	*buf; // Start of buffer
	us_frame_s	*frame;
} _jpeg_dest_manager_s;


static void _jpeg_set_dest_frame(j_compress_ptr jpeg, us_frame_s *frame);

static void _jpeg_write_scanlines_yuyv(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_yvyu(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_uyvy(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_rgb565(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_rgb24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_bgr24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);

static void _jpeg_init_destination(j_compress_ptr jpeg);
static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg);
static void _jpeg_term_destination(j_compress_ptr jpeg);


void us_cpu_encoder_compress(const us_frame_s *src, us_frame_s *dest, unsigned quality) {
	// This function based on compress_image_to_jpeg() from mjpg-streamer

	us_frame_encoding_begin(src, dest, V4L2_PIX_FMT_JPEG);

	struct jpeg_compress_struct jpeg;
	struct jpeg_error_mgr jpeg_error;

	jpeg.err = jpeg_std_error(&jpeg_error);
	jpeg_create_compress(&jpeg);

	_jpeg_set_dest_frame(&jpeg, dest);

	jpeg.image_width = src->width;
	jpeg.image_height = src->height;
	jpeg.input_components = 3;
	jpeg.in_color_space = ((src->format == V4L2_PIX_FMT_YUYV || src->format == V4L2_PIX_FMT_UYVY) ? JCS_YCbCr : JCS_RGB);

	jpeg_set_defaults(&jpeg);
	jpeg_set_quality(&jpeg, quality, TRUE);

	jpeg_start_compress(&jpeg, TRUE);

#	define WRITE_SCANLINES(x_format, x_func) \
		case x_format: { x_func(&jpeg, src); break; }

	switch (src->format) {
		// https://www.fourcc.org/yuv.php
		WRITE_SCANLINES(V4L2_PIX_FMT_YUYV, _jpeg_write_scanlines_yuyv);
		WRITE_SCANLINES(V4L2_PIX_FMT_YVYU, _jpeg_write_scanlines_yvyu);
		WRITE_SCANLINES(V4L2_PIX_FMT_UYVY, _jpeg_write_scanlines_uyvy);
		WRITE_SCANLINES(V4L2_PIX_FMT_RGB565, _jpeg_write_scanlines_rgb565);
		WRITE_SCANLINES(V4L2_PIX_FMT_RGB24, _jpeg_write_scanlines_rgb24);
		WRITE_SCANLINES(V4L2_PIX_FMT_BGR24, _jpeg_write_scanlines_bgr24);
		default: assert(0 && "Unsupported input format for CPU encoder");
	}

#	undef WRITE_SCANLINES

	jpeg_finish_compress(&jpeg);
	jpeg_destroy_compress(&jpeg);

	us_frame_encoding_end(dest);
}

static void _jpeg_set_dest_frame(j_compress_ptr jpeg, us_frame_s *frame) {
	if (jpeg->dest == NULL) {
		assert((jpeg->dest = (struct jpeg_destination_mgr *)(*jpeg->mem->alloc_small)(
			(j_common_ptr) jpeg, JPOOL_PERMANENT, sizeof(_jpeg_dest_manager_s)
		)) != NULL);
	}

	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s *)jpeg->dest;
	dest->mgr.init_destination = _jpeg_init_destination;
	dest->mgr.empty_output_buffer = _jpeg_empty_output_buffer;
	dest->mgr.term_destination = _jpeg_term_destination;
	dest->frame = frame;

	frame->used = 0;
}

static void _jpeg_write_scanlines_yuyv(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	uint8_t *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const unsigned padding = us_frame_get_padding(frame);
	const uint8_t *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		uint8_t *ptr = line_buf;

		for (unsigned x = 0; x < frame->width; ++x) {
			// See also: https://www.kernel.org/doc/html/v4.8/media/uapi/v4l/pixfmt-yuyv.html
			const bool is_odd_pixel = x & 1;
			const uint8_t y = data[is_odd_pixel ? 2 : 0];
			const uint8_t u = data[1];
			const uint8_t v = data[3];

			ptr[0] = y;
			ptr[1] = u;
			ptr[2] = v;
			ptr += 3;

			data += (is_odd_pixel ? 4: 0);
		}
		data += padding;

		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buf);
}

static void _jpeg_write_scanlines_yvyu(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	uint8_t *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const unsigned padding = us_frame_get_padding(frame);
	const uint8_t *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		uint8_t *ptr = line_buf;

		for (unsigned x = 0; x < frame->width; ++x) {
			// See also: https://www.kernel.org/doc/html/v4.8/media/uapi/v4l/pixfmt-yuyv.html
			const bool is_odd_pixel = x & 1;
			const uint8_t y = data[is_odd_pixel ? 2 : 0];
			const uint8_t u = data[3];
			const uint8_t v = data[1];

			ptr[0] = y;
			ptr[1] = u;
			ptr[2] = v;
			ptr += 3;

			data += (is_odd_pixel ? 4 : 0);
		}
		data += padding;

		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buf);
}

static void _jpeg_write_scanlines_uyvy(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	uint8_t *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const unsigned padding = us_frame_get_padding(frame);
	const uint8_t *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		uint8_t *ptr = line_buf;

		for (unsigned x = 0; x < frame->width; ++x) {
			// See also: https://www.kernel.org/doc/html/v4.8/media/uapi/v4l/pixfmt-uyvy.html
			const bool is_odd_pixel = x & 1;
			const uint8_t y = data[is_odd_pixel ? 3 : 1];
			const uint8_t u = data[0];
			const uint8_t v = data[2];

			ptr[0] = y;
			ptr[1] = u;
			ptr[2] = v;
			ptr += 3;

			data += (is_odd_pixel ? 4 : 0);
		}
		data += padding;

		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buf);
}

static void _jpeg_write_scanlines_rgb565(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	uint8_t *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const unsigned padding = us_frame_get_padding(frame);
	const uint8_t *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		uint8_t *ptr = line_buf;

		for (unsigned x = 0; x < frame->width; ++x) {
			const unsigned int two_byte = (data[1] << 8) + data[0];

			ptr[0] = data[1] & 248; // Red
			ptr[1] = (uint8_t)((two_byte & 2016) >> 3); // Green
			ptr[2] = (data[0] & 31) * 8; // Blue
			ptr += 3;

			data += 2;
		}
		data += padding;

		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buf);
}

static void _jpeg_write_scanlines_rgb24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	const unsigned padding = us_frame_get_padding(frame);
	uint8_t *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		JSAMPROW scanlines[1] = {data};
		jpeg_write_scanlines(jpeg, scanlines, 1);

		data += (frame->width * 3) + padding;
	}
}

static void _jpeg_write_scanlines_bgr24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	uint8_t *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const unsigned padding = us_frame_get_padding(frame);
	uint8_t *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		uint8_t *ptr = line_buf;

		// swap B and R values
		for (unsigned x = 0; x < frame->width * 3; x += 3) {
			ptr[0] = data[x + 2];
			ptr[1] = data[x + 1];
			ptr[2] = data[x];
			ptr += 3;
		}
		
		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);

		data += (frame->width * 3) + padding;
	}

	free(line_buf);
}

#define JPEG_OUTPUT_BUFFER_SIZE ((size_t)4096)

static void _jpeg_init_destination(j_compress_ptr jpeg) {
	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s *)jpeg->dest;

	// Allocate the output buffer - it will be released when done with image
	assert((dest->buf = (JOCTET *)(*jpeg->mem->alloc_small)(
		(j_common_ptr) jpeg, JPOOL_IMAGE, JPEG_OUTPUT_BUFFER_SIZE * sizeof(JOCTET)
	)) != NULL);

	dest->mgr.next_output_byte = dest->buf;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;
}

static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg) {
	// Called whenever local jpeg buffer fills up

	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s *)jpeg->dest;

	us_frame_append_data(dest->frame, dest->buf, JPEG_OUTPUT_BUFFER_SIZE);

	dest->mgr.next_output_byte = dest->buf;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;

	return TRUE;
}

static void _jpeg_term_destination(j_compress_ptr jpeg) {
	// Called by jpeg_finish_compress after all data has been written.
	// Usually needs to flush buffer.

	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s *)jpeg->dest;
	const size_t final = JPEG_OUTPUT_BUFFER_SIZE - dest->mgr.free_in_buffer;

	// Write any data remaining in the buffer.
	us_frame_append_data(dest->frame, dest->buf, final);
}

#undef JPEG_OUTPUT_BUFFER_SIZE
