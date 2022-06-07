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
#include <inttypes.h>
#include <assert.h>

#include <sys/types.h>
#include <linux/videodev2.h>

#include <pthread.h>

#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/frame.h"
#include "uslibs/base64.h"

#include "rtp.h"


typedef struct {
	rtp_s			*rtp;
	rtp_callback_f	callback;
	frame_s			*sps; // Actually not a frame, just a bytes storage
	frame_s			*pps;
	pthread_mutex_t	mutex;
} rtpv_s;


rtpv_s *rtpv_init(rtp_callback_f callback);
void rtpv_destroy(rtpv_s *rtpv);

char *rtpv_make_sdp(rtpv_s *rtpv);
void rtpv_wrap(rtpv_s *rtpv, const frame_s *frame);
