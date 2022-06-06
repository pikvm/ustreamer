/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>

#include <sys/mman.h>

#include <linux/videodev2.h>

#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/xioctl.h"


typedef struct {
	uint8_t	*data;
	size_t	allocated;
} m2m_buffer_s;

typedef struct {
	char		*name;
	uint32_t	id;
	int32_t		value;
} m2m_option_s;

typedef struct {
	int				fd;
	m2m_buffer_s	*input_bufs;
	unsigned		n_input_bufs;
	m2m_buffer_s	*output_bufs;
	unsigned		n_output_bufs;

	unsigned	width;
	unsigned	height;
	unsigned	input_format;
	unsigned	stride;
	bool		dma;
	bool		ready;

	int last_online;
} m2m_encoder_runtime_s;

typedef struct {
	char			*name;
	char			*path;
	unsigned		output_format;
	unsigned		fps;
	bool			allow_dma;
	m2m_option_s	*options;

	m2m_encoder_runtime_s *run;
} m2m_encoder_s;


m2m_encoder_s *m2m_h264_encoder_init(const char *name, const char *path, unsigned bitrate, unsigned gop);
m2m_encoder_s *m2m_mjpeg_encoder_init(const char *name, const char *path, unsigned quality);
m2m_encoder_s *m2m_jpeg_encoder_init(const char *name, const char *path, unsigned quality);
void m2m_encoder_destroy(m2m_encoder_s *enc);

int m2m_encoder_compress(m2m_encoder_s *enc, const frame_s *src, frame_s *dest, bool force_key);
