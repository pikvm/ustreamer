/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
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


#include "frametext.h"

#include <string.h>

#include <sys/types.h>

#include <linux/videodev2.h>

#include "tools.h"
#include "frame.h"
#include "frametext_font.h"


static void _frametext_draw_line(
	us_frametext_s *ft, const char *line,
	uint scale_x, uint scale_y,
	uint start_x, uint start_y);


us_frametext_s *us_frametext_init(void) {
	us_frametext_s *ft;
	US_CALLOC(ft, 1);
	ft->frame = us_frame_init();
	return ft;
}

void us_frametext_destroy(us_frametext_s *ft) {
	us_frame_destroy(ft->frame);
	US_DELETE(ft->text, free);
	free(ft);
}

/*
Every character in the font is encoded row-wise in 8 bytes.
The least significant bit of each byte corresponds to the first pixel in a row. 
The character 'A' (0x41 / 65) is encoded as { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}

    0x0C => 0000 1100 => ..XX....
    0X1E => 0001 1110 => .XXXX...
    0x33 => 0011 0011 => XX..XX..
    0x33 => 0011 0011 => XX..XX..
    0x3F => 0011 1111 => xxxxxx..
    0x33 => 0011 0011 => XX..XX..
    0x33 => 0011 0011 => XX..XX..
    0x00 => 0000 0000 => ........

To access the nth pixel in a row, right-shift by n.

                         . . X X . . . .
                         | | | | | | | |
    (0x0C >> 0) & 1 == 0-+ | | | | | | |
    (0x0C >> 1) & 1 == 0---+ | | | | | |
    (0x0C >> 2) & 1 == 1-----+ | | | | |
    (0x0C >> 3) & 1 == 1-------+ | | | |
    (0x0C >> 4) & 1 == 0---------+ | | |
    (0x0C >> 5) & 1 == 0-----------+ | |
    (0x0C >> 6) & 1 == 0-------------+ |
    (0x0C >> 7) & 1 == 0---------------+
*/

void us_frametext_draw(us_frametext_s *ft, const char *text, uint width, uint height) {
	assert(width > 0);
	assert(height > 0);

	us_frame_s *const frame = ft->frame;

	if (
		frame->width == width && frame->height == height
		&& ft->text != NULL && !strcmp(ft->text, text)
	) {
		return;
	}

	US_DELETE(ft->text, free);
	ft->text = us_strdup(text);
	strcpy(ft->text, text);
	frame->width = width;
	frame->height = height;
	frame->format = V4L2_PIX_FMT_RGB24;
	frame->stride = width * 3;
	frame->used = width * height * 3;
	us_frame_realloc_data(frame, frame->used);
	memset(frame->data, 0, frame->used);

	if (frame->width == 0 || frame->height == 0) {
		return;
	}

	char *str = us_strdup(text);
	char *line;
	char *rest;

	uint block_width = 0;
	uint block_height = 0;
	while ((line = strtok_r((block_height == 0 ? str : NULL), "\n", &rest)) != NULL) {
		block_width = US_MAX(strlen(line) * 8, block_width);
		block_height += 8;
	}
	if (block_width == 0 || block_height == 0) {
		goto empty;
	}

	// Ширина текста должна быть от 75%, до половины экрана, в зависимости от длины
	const float div_x = US_MAX(US_MIN((100 / block_width * 2), 2.0), 1.5);

	// Высоту тоже отрегулировать как-нибудь
	const float div_y = US_MAX(US_MIN((70 / block_height * 2), 2.0), 1.5);

	uint scale_x = frame->width / block_width / div_x;
	uint scale_y = frame->height / block_height / div_y;
	if (scale_x < scale_y / 1.5) { // Keep proportions
		scale_y = scale_x * 1.5;
	} else if (scale_y < scale_x * 1.5) {
		scale_x = scale_y / 1.5;
	}

	strcpy(str, text);

	const uint start_y = (frame->height >= (block_height * scale_y)
		? ((frame->height - (block_height * scale_y)) / 2)
		: 0);
	uint n_line = 0;
	while ((line = strtok_r((n_line == 0 ? str : NULL), "\n", &rest)) != NULL) {
		const uint line_width = strlen(line) * 8 * scale_x;
		const uint start_x = (frame->width >= line_width
			? ((frame->width - line_width) / 2)
			: 0);
		_frametext_draw_line(ft, line, scale_x, scale_y, start_x, start_y + n_line * 8 * scale_y);
		++n_line;
	}

empty:
	free(str);
}

void _frametext_draw_line(
	us_frametext_s *ft, const char *line,
	uint scale_x, uint scale_y,
	uint start_x, uint start_y) {

	us_frame_s *const frame = ft->frame;

	const size_t len = strlen(line);

	for (uint ch_y = 0; ch_y < 8 * scale_y; ++ch_y) {
		const uint canvas_y = start_y + ch_y;
		for (uint ch_x = 0; ch_x < 8 * len * scale_x; ++ch_x) {
			if ((start_x + ch_x) >= frame->width) {
				break;
			}
			const uint canvas_x = (start_x + ch_x) * 3;
			const uint offset = canvas_y * frame->stride + canvas_x;
			if (offset >= frame->used) {
				break;
			}

			const u8 ch = US_MIN((u8)line[ch_x / 8 / scale_x], sizeof(US_FRAMETEXT_FONT) / 8 - 1);
			const uint ch_byte = (ch_y / scale_y) % 8;
			const uint ch_bit = (ch_x / scale_x) % 8;
			const bool pix_on = !!(US_FRAMETEXT_FONT[ch][ch_byte] & (1 << ch_bit));

			u8 *const r = &frame->data[offset];
			u8 *const g = r + 1;
			u8 *const b = r + 2;

			*r = pix_on * 0x65; // RGB/BGR-friendly
			*g = pix_on * 0x65;
			*b = pix_on * 0x65;
		}
	}
}
