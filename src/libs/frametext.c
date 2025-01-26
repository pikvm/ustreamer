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

void us_frametext_draw(us_frametext_s *ft, const char *text1, const char *text2, uint width, uint height) {
	assert(width > 0);
	assert(height > 0);

	us_frame_s *const frame = ft->frame;

	// 检查是否需要重新分配内存或重绘
	if (
		frame->width == width && frame->height == height
		&& ft->text != NULL 
		&& (strcmp(ft->text, text1) == 0 || strcmp(ft->text, text2) == 0)
	) {
		return;
	}

	US_DELETE(ft->text, free); // 清除旧的文本

	// 初始化新的帧数据
	frame->width = width;
	frame->height = height;
	frame->format = V4L2_PIX_FMT_RGB24;
	frame->stride = width * 3;
	frame->used = width * height * 3;
	us_frame_realloc_data(frame, frame->used);
	memset(frame->data, 0, frame->used);

	// 计算每行的宽度和高度
	uint block_width1 = strlen(text1) * 6;
	uint block_height1 = 8;
	uint block_width2 = strlen(text2) * 6;
	uint block_height2 = 4;

	// 计算缩放比例，使第一行更大，第二行更小
	uint scale_x1 = (uint)((float)frame->width / 2 / block_width1 * 1); // 第一行更大的缩放比例
	uint scale_y1 = (uint)((float)frame->height / 2  / (block_height1 + block_height2) * 0.5);
	uint scale_x2 = (uint)((float)frame->width / 2 / block_width2 * 1); // 第二行更小的缩放比例
	uint scale_y2 = (uint)((float)frame->height / 2 / (block_height1 + block_height2) * 0.2);

	scale_x1 = US_MIN(scale_x1, scale_y1);
	scale_x2 = US_MIN(scale_x2, scale_y2);

	// 确保不会超出画面边界
	scale_x1 = US_MIN(scale_x1, (uint)(frame->width / block_width1));
	scale_y1 = US_MIN(scale_y1, (uint)(frame->height / block_height1));
	scale_x2 = US_MIN(scale_x2, (uint)(frame->width / block_width2));
	scale_y2 = US_MIN(scale_y2, (uint)(frame->height / block_height2));

	// 绘制第一行
	const uint start_y1 = (frame->height - (block_height1 * scale_y1 + block_height2 * scale_y2)) / 2;
	_frametext_draw_line(ft, text1, scale_x1, scale_y1, (frame->width - (strlen(text1) * 8 * scale_x1)) / 2, start_y1);

	// 绘制第二行
	const uint start_y2 = start_y1 + block_height1 * scale_y1;
	_frametext_draw_line(ft, text2, scale_x2, scale_y2, (frame->width - (strlen(text2) * 8 * scale_x2)) / 2, start_y2);
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
