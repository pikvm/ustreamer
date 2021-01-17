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
#include <assert.h>

#include <linux/videodev2.h>

#include "../../libs/tools.h"
#include "../../libs/logging.h"
#include "../../libs/frame.h"


typedef struct {
  void *start;
  size_t length;
  struct v4l2_buffer inner;
  struct v4l2_plane planes;
} buffer;

typedef struct {
	int fd;
	buffer* output;
	int output_len;
	buffer capture;

  uint32_t width;
  uint32_t height;

  bool prepared;
  double start_ts;

  int last_online;
  long frame;
} h264_encoder_s;


h264_encoder_s *h264_encoder_init(unsigned bitrate, unsigned gop, unsigned fps);
void h264_encoder_destroy(h264_encoder_s *enc);

bool h264_encoder_is_prepared_for(h264_encoder_s *enc, const frame_s *frame, bool zero_copy);
int h264_encoder_prepare(h264_encoder_s *enc, const frame_s *frame, bool zero_copy);
int h264_encoder_compress(h264_encoder_s *enc, const frame_s *src, int src_vcsm_handle, frame_s *dest, bool force_key);
