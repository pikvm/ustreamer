/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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


#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>

#include <sys/mman.h>

#include <linux/videodev2.h>

#include "../../libs/tools.h"
#include "../../libs/logging.h"
#include "../../libs/frame.h"

#include "../xioctl.h"


typedef struct {
	uint8_t	*data;
	size_t	allocated;
} h264_buffer_s;

typedef struct {
	char		*path;
	unsigned	bitrate; // Kbit-per-sec
	unsigned	gop; // Interval between keyframes
	unsigned	fps;

	int				fd;
	h264_buffer_s	*input_bufs;
	unsigned		n_input_bufs;
	h264_buffer_s	*output_bufs;
	unsigned		n_output_bufs;

	int last_online;

	unsigned	width;
	unsigned	height;
	unsigned	format;
	unsigned	stride;
	bool		dma;
	bool		ready;
} h264_encoder_s;


h264_encoder_s *h264_encoder_init(const char *path, unsigned bitrate, unsigned gop, unsigned fps);
void h264_encoder_destroy(h264_encoder_s *enc);

bool h264_encoder_is_prepared_for(h264_encoder_s *enc, const frame_s *frame, bool dma);
int h264_encoder_prepare(h264_encoder_s *enc, const frame_s *frame, bool dma);
int h264_encoder_compress(h264_encoder_s *enc, const frame_s *src, int src_dma_fd, frame_s *dest, bool force_key);
