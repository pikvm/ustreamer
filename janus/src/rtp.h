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

#include "uslibs/types.h"


// https://stackoverflow.com/questions/47635545/why-webrtc-chose-rtp-max-packet-size-to-1200-bytes
#define US_RTP_DATAGRAM_SIZE	1200
#define US_RTP_HEADER_SIZE		12
#define US_RTP_PAYLOAD_SIZE		(US_RTP_DATAGRAM_SIZE - US_RTP_HEADER_SIZE)

#define US_RTP_H264_PAYLOAD		96
#define US_RTP_OPUS_PAYLOAD		111

#define US_RTP_OPUS_HZ			48000
#define US_RTP_OPUS_CH			2


typedef struct {
	uint	payload;
	bool	video;
	u32		ssrc;

	u16		seq;
	u8		datagram[US_RTP_DATAGRAM_SIZE];
	uz		used;
	bool	zero_playout_delay;
} us_rtp_s;

typedef void (*us_rtp_callback_f)(const us_rtp_s *rtp);


us_rtp_s *us_rtp_init(void);
void us_rtp_destroy(us_rtp_s *rtp);

void us_rtp_assign(us_rtp_s *rtp, uint payload, bool video);
void us_rtp_write_header(us_rtp_s *rtp, u32 pts, bool marked);
