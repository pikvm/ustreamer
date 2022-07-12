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
#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>

#include "uslibs/tools.h"


// https://stackoverflow.com/questions/47635545/why-webrtc-chose-rtp-max-packet-size-to-1200-bytes
#define RTP_DATAGRAM_SIZE	1200
#define RTP_HEADER_SIZE		12


typedef struct {
	unsigned	payload;
	bool		video;
	uint32_t	ssrc;

	uint16_t	seq;
	uint8_t		datagram[RTP_DATAGRAM_SIZE];
	size_t		used;
} rtp_s;

typedef void (*rtp_callback_f)(const rtp_s *rtp);


rtp_s *rtp_init(unsigned payload, bool video);
rtp_s *rtp_dup(const rtp_s *rtp);
void rtp_destroy(rtp_s *rtp);

void rtp_write_header(rtp_s *rtp, uint32_t pts, bool marked);
