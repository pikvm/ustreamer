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


#pragma once

#include "../libs/types.h"
#include "../libs/frame.h"


typedef struct {
	u8	*data;
	uz	allocated;
} us_m2m_buffer_s;

typedef struct {
	int				fd;
	uint			fps_limit;
	us_m2m_buffer_s	*input_bufs;
	uint			n_input_bufs;
	us_m2m_buffer_s	*output_bufs;
	uint			n_output_bufs;

	uint	p_width;
	uint	p_height;
	uint	p_input_format;
	uint	p_stride;
	bool	p_dma;

	bool	ready;
	int		last_online;
	ldf		last_encode_ts;
} us_m2m_encoder_runtime_s;

typedef struct {
	char	*name;
	char	*path;
	uint	output_format;
	uint	bitrate;
	uint	gop;
	uint	quality;
	bool	allow_dma;

	us_m2m_encoder_runtime_s *run;
} us_m2m_encoder_s;


us_m2m_encoder_s *us_m2m_h264_encoder_init(const char *name, const char *path, uint bitrate, uint gop);
us_m2m_encoder_s *us_m2m_mjpeg_encoder_init(const char *name, const char *path, uint quality);
us_m2m_encoder_s *us_m2m_jpeg_encoder_init(const char *name, const char *path, uint quality);
void us_m2m_encoder_destroy(us_m2m_encoder_s *enc);

int us_m2m_encoder_compress(us_m2m_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);
