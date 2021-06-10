/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    This source file is partially based on this code:                       #
#      - https://github.com/catid/kvm/blob/master/kvm_pipeline/src           #
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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <linux/videodev2.h>

#include <pthread.h>

#include "tools.h"
#include "threading.h"
#include "frame.h"
#include "base64.h"


// https://stackoverflow.com/questions/47635545/why-webrtc-chose-rtp-max-packet-size-to-1200-bytes
#define RTP_DATAGRAM_SIZE 1200


typedef struct {
	uint32_t ssrc;
	uint16_t seq;

	uint8_t datagram[RTP_DATAGRAM_SIZE];

	frame_s *sps; // Actually not a frame, just a bytes storage
	frame_s *pps;

	pthread_mutex_t mutex;
} rtp_s;

typedef void (*rtp_callback_f)(const uint8_t *datagram, size_t size);


rtp_s *rtp_init(void);
void rtp_destroy(rtp_s *rtp);

char *rtp_make_sdp(rtp_s *rtp);
void rtp_wrap_h264(rtp_s *rtp, const frame_s *frame, rtp_callback_f callback);
