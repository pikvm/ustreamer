/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file based on code of MJPG-Streamer.                        #
#                                                                            #
#    Copyright (C) 2005-2006  Laurent Pinchart & Michel Xhaard               #
#    Copyright (C) 2006  Gabriel A. Devenyi                                  #
#    Copyright (C) 2007  Tom Stöveken                                        #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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
	struct jpeg_destination_mgr mgr; // Default manager
	JOCTET		*buf; // Start of buffer
	us_frame_s	*frame;
} _jpeg_dest_manager_s;


static void _jpeg_set_dest_frame(j_compress_ptr jpeg, us_frame_s *frame);

static void _jpeg_write_scanlines_yuv(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_yuv_planar(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_grey(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_rgb565(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_rgb24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
#ifndef JCS_EXTENSIONS
#warning JCS_EXT_BGR is not supported, please use libjpeg-turbo
static void _jpeg_write_scanlines_bgr24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
#endif
static void _jpeg_write_scanlines_nv12(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_nv16(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);
static void _jpeg_write_scanlines_nv24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame);

static void _nv12_to_rgb24(const u8 *in, u8 *out, int width, int height);
static void _nv16_to_rgb24(const u8 *in, u8 *out, int width, int height);
static void _nv24_to_rgb24(const u8 *in, u8 *out, int width, int height);

static void _jpeg_init_destination(j_compress_ptr jpeg);
static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg);
static void _jpeg_term_destination(j_compress_ptr jpeg);


void us_cpu_encoder_compress(const us_frame_s *src, us_frame_s *dest, uint quality) {
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
	switch (src->format) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
			jpeg.in_color_space = JCS_YCbCr;
			break;
		case V4L2_PIX_FMT_GREY:
			jpeg.input_components = 1;
			jpeg.in_color_space = JCS_GRAYSCALE;
			break;
#		ifdef JCS_EXTENSIONS
		case V4L2_PIX_FMT_BGR24:
			jpeg.in_color_space = JCS_EXT_BGR;
			break;
#		endif
		default:
			jpeg.in_color_space = JCS_RGB;
			break;
	}

	jpeg_set_defaults(&jpeg);
	jpeg_set_quality(&jpeg, quality, TRUE);

	jpeg_start_compress(&jpeg, TRUE);

	switch (src->format) {
		// https://www.fourcc.org/yuv.php
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
			_jpeg_write_scanlines_yuv(&jpeg, src);
			break;

		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
			_jpeg_write_scanlines_yuv_planar(&jpeg, src);
			break;

		case V4L2_PIX_FMT_NV12:
			_jpeg_write_scanlines_nv12(&jpeg, src);
			break;

		case V4L2_PIX_FMT_NV16:
			_jpeg_write_scanlines_nv16(&jpeg, src);
			break;

		case V4L2_PIX_FMT_NV24:
			_jpeg_write_scanlines_nv24(&jpeg, src);
			break;
		
		case V4L2_PIX_FMT_GREY:
			_jpeg_write_scanlines_grey(&jpeg, src);
			break;

		case V4L2_PIX_FMT_RGB565:
			_jpeg_write_scanlines_rgb565(&jpeg, src);
			break;

		case V4L2_PIX_FMT_RGB24:
			_jpeg_write_scanlines_rgb24(&jpeg, src);
			break;

		case V4L2_PIX_FMT_BGR24:
#			ifdef JCS_EXTENSIONS
			_jpeg_write_scanlines_rgb24(&jpeg, src); // Use native JCS_EXT_BGR
#			else
			_jpeg_write_scanlines_bgr24(&jpeg, src);
#			endif
			break;
		default: assert(0 && "Unsupported input format for CPU encoder"); return;
	}

	jpeg_finish_compress(&jpeg);
	jpeg_destroy_compress(&jpeg);

	us_frame_encoding_end(dest);
}

static void _jpeg_set_dest_frame(j_compress_ptr jpeg, us_frame_s *frame) {
	if (jpeg->dest == NULL) {
		assert((jpeg->dest = (struct jpeg_destination_mgr*)(*jpeg->mem->alloc_small)(
			(j_common_ptr) jpeg, JPOOL_PERMANENT, sizeof(_jpeg_dest_manager_s)
		)) != NULL);
	}

	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s*)jpeg->dest;
	dest->mgr.init_destination = _jpeg_init_destination;
	dest->mgr.empty_output_buffer = _jpeg_empty_output_buffer;
	dest->mgr.term_destination = _jpeg_term_destination;
	dest->frame = frame;

	frame->used = 0;
}

static void _jpeg_write_scanlines_yuv(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	u8 *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const uint padding = us_frame_get_padding(frame);
	const u8 *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		u8 *ptr = line_buf;

		for (uint x = 0; x < frame->width; ++x) {
			// See also: https://www.kernel.org/doc/html/v4.8/media/uapi/v4l/pixfmt-uyvy.html
			const bool is_odd_pixel = x & 1;
			u8 y, u, v;
			if (frame->format == V4L2_PIX_FMT_YUYV) {
				y = data[is_odd_pixel ? 2 : 0];
				u = data[1];
				v = data[3];
			} else if (frame->format == V4L2_PIX_FMT_YVYU) {
				y = data[is_odd_pixel ? 2 : 0];
				u = data[3];
				v = data[1];
			} else if (frame->format == V4L2_PIX_FMT_UYVY) {
				y = data[is_odd_pixel ? 3 : 1];
				u = data[0];
				v = data[2];
			} else {
				assert(0 && "Unsupported pixel format");
				return; // Makes linter happy
			}

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

static void _jpeg_write_scanlines_yuv_planar(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	u8 *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const uint padding = us_frame_get_padding(frame);
	const uint image_size = frame->width * frame->height;
	const uint chroma_array_size = (frame->used - image_size) / 2;
	const uint chroma_matrix_order = (image_size / chroma_array_size) == 16 ? 4 : 2;
	const u8 *data = frame->data;
	const u8 *chroma1_data = frame->data + image_size;
	const u8 *chroma2_data = frame->data + image_size + chroma_array_size;

	//US_LOG_DEBUG("Planar data: Image Size %u, Chroma Array Size %u, Chroma Matrix Order %u",
	//	image_size, chroma_array_size, chroma_matrix_order);

	while (jpeg->next_scanline < frame->height) {
		u8 *ptr = line_buf;

		for (uint x = 0; x < frame->width; ++x) {
			// See also: https://www.kernel.org/doc/html/v4.8/media/uapi/v4l/pixfmt-yuv420.html
			u8 y = data[x];
			u8 u;
			u8 v;
			uint chroma_position = x / chroma_matrix_order;

			switch (frame->format) {
				case V4L2_PIX_FMT_YUV420:
					u = chroma1_data[chroma_position];
					v = chroma2_data[chroma_position];
					break;
				case V4L2_PIX_FMT_YVU420:
					u = chroma2_data[chroma_position];
					v = chroma1_data[chroma_position];
					break;
				default:
					assert(0 && "Unsupported pixel format");
					return; // Makes linter happy
			}

			ptr[0] = y;
			ptr[1] = u;
			ptr[2] = v;
			ptr += 3;
		}

		data += frame->width + padding;

		if (jpeg->next_scanline > 0 && jpeg->next_scanline % chroma_matrix_order == 0) {
			chroma1_data += (frame->width + padding) / chroma_matrix_order;
			chroma2_data += (frame->width + padding) / chroma_matrix_order;
		}

		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buf);
}

static void _jpeg_write_scanlines_grey(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	u8 *line_buf;
	US_CALLOC(line_buf, frame->width);

	const uint padding = us_frame_get_padding(frame);
	const u8 *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		u8 *ptr = line_buf;

		for (uint x = 0; x < frame->width; ++x) {
			ptr[0] = data[x];
			ptr += 1;
		}

		data += frame->width + padding;

		JSAMPROW scanlines[1] = {line_buf};
		jpeg_write_scanlines(jpeg, scanlines, 1);
	}

	free(line_buf);
}

static void _jpeg_write_scanlines_rgb565(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	u8 *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const uint padding = us_frame_get_padding(frame);
	const u8 *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		u8 *ptr = line_buf;

		for (uint x = 0; x < frame->width; ++x) {
			const uint two_byte = (data[1] << 8) + data[0];

			ptr[0] = data[1] & 248; // Red
			ptr[1] = (u8)((two_byte & 2016) >> 3); // Green
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
	const uint padding = us_frame_get_padding(frame);
	u8 *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		JSAMPROW scanlines[1] = {data};
		jpeg_write_scanlines(jpeg, scanlines, 1);

		data += (frame->width * 3) + padding;
	}
}

#ifndef JCS_EXTENSIONS
static void _jpeg_write_scanlines_bgr24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	u8 *line_buf;
	US_CALLOC(line_buf, frame->width * 3);

	const uint padding = us_frame_get_padding(frame);
	u8 *data = frame->data;

	while (jpeg->next_scanline < frame->height) {
		u8 *ptr = line_buf;

		// swap B and R values
		for (uint x = 0; x < frame->width * 3; x += 3) {
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
#endif

#define JPEG_OUTPUT_BUFFER_SIZE ((size_t)4096)

static void _jpeg_init_destination(j_compress_ptr jpeg) {
	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s*)jpeg->dest;

	// Allocate the output buffer - it will be released when done with image
	assert((dest->buf = (JOCTET*)(*jpeg->mem->alloc_small)(
		(j_common_ptr) jpeg, JPOOL_IMAGE, JPEG_OUTPUT_BUFFER_SIZE * sizeof(JOCTET)
	)) != NULL);

	dest->mgr.next_output_byte = dest->buf;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;
}

static boolean _jpeg_empty_output_buffer(j_compress_ptr jpeg) {
	// Called whenever local jpeg buffer fills up

	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s*)jpeg->dest;

	us_frame_append_data(dest->frame, dest->buf, JPEG_OUTPUT_BUFFER_SIZE);

	dest->mgr.next_output_byte = dest->buf;
	dest->mgr.free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;

	return TRUE;
}

static void _jpeg_term_destination(j_compress_ptr jpeg) {
	// Called by jpeg_finish_compress after all data has been written.
	// Usually needs to flush buffer.

	_jpeg_dest_manager_s *const dest = (_jpeg_dest_manager_s*)jpeg->dest;
	const size_t final = JPEG_OUTPUT_BUFFER_SIZE - dest->mgr.free_in_buffer;

	// Write any data remaining in the buffer.
	us_frame_append_data(dest->frame, dest->buf, final);
}

static void _nv12_to_rgb24(const u8 *in, u8 *out, int width, int height) {
	int frame_size = width * height;
	int uv_offset = frame_size;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			int uv_index = (i / 2) * width + 2 * (j / 2);
			int Y = in[i * width + j];
			int U = in[uv_offset + uv_index];
			int V = in[uv_offset + uv_index + 1];

			// YUV to RGB conversion
			int C = Y - 16;
			int D = U - 128;
			int E = V - 128;
			int R = (298 * C + 409 * E + 128) >> 8;
			int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
			int B = (298 * C + 516 * D + 128) >> 8;

			// Clamp values to [0, 255]
			R = (R < 0) ? 0 : ((R > 255) ? 255 : R);
			G = (G < 0) ? 0 : ((G > 255) ? 255 : G);
			B = (B < 0) ? 0 : ((B > 255) ? 255 : B);

			int rgb_index = (i * width + j) * 3;
			out[rgb_index] = (u8)R;
			out[rgb_index + 1] = (u8)G;
			out[rgb_index + 2] = (u8)B;
		}
	}
}

static void _nv16_to_rgb24(const u8 *in, u8 *out, int width, int height) {
	int frame_size = width * height;
	int uv_offset = frame_size;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			int y_index = i * width + j;
			int uv_index = uv_offset + (i * width) + (j & ~1);
			int Y = in[y_index];
			int U = in[uv_index] - 128;
			int V = in[uv_index + 1] - 128;

			// YUV to RGB conversion
			int R = (298 * (Y - 16) + 409 * V + 128) >> 8;
			int G = (298 * (Y - 16) - 100 * U - 208 * V + 128) >> 8;
			int B = (298 * (Y - 16) + 516 * U + 128) >> 8;

			// Clamp values to [0, 255]
			R = R < 0 ? 0 : (R > 255 ? 255 : R);
			G = G < 0 ? 0 : (G > 255 ? 255 : G);
			B = B < 0 ? 0 : (B > 255 ? 255 : B);

			int rgb_index = y_index * 3;
			out[rgb_index] = (u8)R;
			out[rgb_index + 1] = (u8)G;
			out[rgb_index + 2] = (u8)B;
		}
	}
}

static void _nv24_to_rgb24(const u8 *in, u8 *out, int width, int height) {
	int frame_size = width * height;
	int uv_offset = frame_size;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			int y_index = i * width + j;
			int uv_index = uv_offset + (2 * j) + (i * width * 2);
			int Y = in[y_index];
			int U = in[uv_index] - 128;
			int V = in[uv_index + 1] - 128;

			// YUV to RGB conversion
			int R = (298 * (Y - 16) + 409 * V + 128) >> 8;
			int G = (298 * (Y - 16) - 100 * U - 208 * V + 128) >> 8;
			int B = (298 * (Y - 16) + 516 * U + 128) >> 8;

			// Clamp values to [0, 255]
			R = R < 0 ? 0 : (R > 255 ? 255 : R);
			G = G < 0 ? 0 : (G > 255 ? 255 : G);
			B = B < 0 ? 0 : (B > 255 ? 255 : B);

			int rgb_index = y_index * 3;
			out[rgb_index] = (u8)R;
			out[rgb_index + 1] = (u8)G;
			out[rgb_index + 2] = (u8)B;
		}
	}
}

static void _jpeg_write_scanlines_nv12(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	const uint padding = us_frame_get_padding(frame);
	u8 *rgb;
	US_CALLOC(rgb, frame->width * frame->height * 3);
	_nv12_to_rgb24(frame->data, rgb, frame->width, frame->height);
	u8 *data = rgb;

	while (jpeg->next_scanline < frame->height) {
		JSAMPROW scanlines[1] = {data};
		jpeg_write_scanlines(jpeg, scanlines, 1);

		data += (frame->width * 3) + padding;
	}

	free(rgb);
}

static void _jpeg_write_scanlines_nv16(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	const uint padding = us_frame_get_padding(frame);
	u8 *rgb;
	US_CALLOC(rgb, frame->width * frame->height * 3);
	_nv16_to_rgb24(frame->data, rgb, frame->width, frame->height);
	u8 *data = rgb;

	while (jpeg->next_scanline < frame->height) {
		JSAMPROW scanlines[1] = {data};
		jpeg_write_scanlines(jpeg, scanlines, 1);

		data += (frame->width * 3) + padding;
	}

	free(rgb);
}

static void _jpeg_write_scanlines_nv24(struct jpeg_compress_struct *jpeg, const us_frame_s *frame) {
	const uint padding = us_frame_get_padding(frame);
	u8 *rgb;
	US_CALLOC(rgb, frame->width * frame->height * 3);
	_nv24_to_rgb24(frame->data, rgb, frame->width, frame->height);
	u8 *data = rgb;

	while (jpeg->next_scanline < frame->height) {
		JSAMPROW scanlines[1] = {data};
		jpeg_write_scanlines(jpeg, scanlines, 1);

		data += (frame->width * 3) + padding;
	}

	free(rgb);
}

#undef JPEG_OUTPUT_BUFFER_SIZE
