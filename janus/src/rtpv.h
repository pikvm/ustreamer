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


#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include <sys/types.h>
#include <linux/videodev2.h>

#include "uslibs/tools.h"
#include "uslibs/frame.h"

#include "rtp.h"


typedef struct {
	us_rtp_s			*rtp;
	us_rtp_callback_f	callback;
} us_rtpv_s;


us_rtpv_s *us_rtpv_init(us_rtp_callback_f callback);
void us_rtpv_destroy(us_rtpv_s *rtpv);

char *us_rtpv_make_sdp(us_rtpv_s *rtpv);
void us_rtpv_wrap(us_rtpv_s *rtpv, const us_frame_s *frame);
