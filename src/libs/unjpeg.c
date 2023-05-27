/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
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


#include "unjpeg.h"


typedef struct {
	struct jpeg_error_mgr	mgr; // Default manager
	jmp_buf					jmp;
	const us_frame_s		*frame;
} _jpeg_error_manager_s;


static void _jpeg_error_handler(j_common_ptr jpeg);


int us_unjpeg(const us_frame_s *src, us_frame_s *dest, bool decode) {
	assert(us_is_jpeg(src->format));

	volatile int retval = 0;

	struct jpeg_decompress_struct jpeg;
	jpeg_create_decompress(&jpeg);

	// https://stackoverflow.com/questions/19857766/error-handling-in-libjpeg
	_jpeg_error_manager_s jpeg_error;
	jpeg.err = jpeg_std_error((struct jpeg_error_mgr *)&jpeg_error);
	jpeg_error.mgr.error_exit = _jpeg_error_handler;
	jpeg_error.frame = src;
	if (setjmp(jpeg_error.jmp) < 0) {
		retval = -1;
		goto done;
	}

	jpeg_mem_src(&jpeg, src->data, src->used);
	jpeg_read_header(&jpeg, TRUE);
	jpeg.out_color_space = JCS_RGB;

	jpeg_start_decompress(&jpeg);

	us_frame_copy_meta(src, dest);
	dest->format = V4L2_PIX_FMT_RGB24;
	dest->width = jpeg.output_width;
	dest->height = jpeg.output_height;
	dest->stride = jpeg.output_width * jpeg.output_components; // Row stride
	dest->used = 0;

	if (decode) {
		JSAMPARRAY scanlines;
		scanlines = (*jpeg.mem->alloc_sarray)((j_common_ptr) &jpeg, JPOOL_IMAGE, dest->stride, 1);

		us_frame_realloc_data(dest, ((dest->width * dest->height) << 1) * 2);
		while (jpeg.output_scanline < jpeg.output_height) {
			jpeg_read_scanlines(&jpeg, scanlines, 1);
			us_frame_append_data(dest, scanlines[0], dest->stride);
		}

		jpeg_finish_decompress(&jpeg);
	}

	done:
		jpeg_destroy_decompress(&jpeg);
		return retval;
}

static void _jpeg_error_handler(j_common_ptr jpeg) {
	_jpeg_error_manager_s *jpeg_error = (_jpeg_error_manager_s *)jpeg->err;
	char msg[JMSG_LENGTH_MAX];

	(*jpeg_error->mgr.format_message)(jpeg, msg);
	US_LOG_ERROR("Can't decompress JPEG: %s", msg);
	longjmp(jpeg_error->jmp, -1);
}
