/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C)  2026  Maxim Devaev <mdevaev@gmail.com>                   #
#    Copyright (C)  2026  Sergey Radionov <rsatom@gmail.com>                 #
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

#include <janus/rtp.h>

#include "uslibs/types.h"
#include "uslibs/frame.h"


// Should return:
// * input frame
// * replacement frame - to use it in following parsing
typedef us_frame_s *(*us_frame_callback_f)(us_frame_s *frame, void *user_data);

typedef struct { // *_rts == [R]TP [T]ime[S]tamp
	us_frame_callback_f		callback;
	void					*user_data;

	bool		had_packets; // Были ли вообще какие-то пакеты когда-то, взводится один раз

	ldf			reference_local_ts; // Локальное время
	u32			reference_packet_rts;
	ldf			reference_ntp_ts;

	u16			last_packet_seq;
	u32			last_packet_rts; // Время из последнего полученного пакета

	bool		fu;
	u16			fu_seq;
	u32			fu_rts;
	bool		fu_is_bad;

	bool		skip_until_key;

	us_frame_s	*frame;
	uint		frame_width;
	uint		frame_height;
	ldf			frame_ts; // in case of fu, time of very first packet
} us_rtpc_s;

typedef enum {
	US_RUR_SUCCESS = 0,
	US_RUR_BAD_PACKET = -1,
	US_RUR_INVALID_SEQ = -2,
	US_RUR_DEPACKETIZATION_FAILED = -3,
	// Incomplete FU found. Current packet not handled.
	// It's required to call us_rtpc_unwrap one else time with the same packet.
	US_RUR_DEPACKETIZATION_FAILED_RETRY = -4,
	US_RUR_DEPACKETIZATION_SKIPPED = -5,
	US_RUR_UNSUPPORTED_UNIT_TYPE = -6,
} us_rtpc_unwrap_result_e;

us_rtpc_s *us_rtpc_init(us_frame_callback_f callback, void *user_data);
void us_rtpc_destroy(us_rtpc_s *rtpc);

us_rtpc_unwrap_result_e us_rtpc_unwrap(
	us_rtpc_s *rtpc,
	const u8 *payload,
	int payload_size,
	u16 seq,
	u32 rts);

void us_rtpc_sync_timestamp(us_rtpc_s *rtpc, ldf ntp_ts, u32 packet_rts);
