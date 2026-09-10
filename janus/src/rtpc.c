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


#include "rtpc.h"

#include <linux/videodev2.h>

#include "uslibs/logging.h"

#include "h264.h"


// H.264 fragment types. Commented types are not used in WebRTC.
enum {
	_FR_REGULAR_NAL_MIN = 1,
	_FR_REGULAR_NAL_MAX = 23,
	_FR_STAP_A = 24,		// Single Time Aggregation Packet, Type A (STAP-A)
	// _FR_STAP_B = 25,		// Single Time Aggregation Packet, Type B (STAP-B)
	// _FR_MTAP_16 = 26,	// Multi-Time Aggregation Packet, Type A (MTAP-16)
	// _FR_MTAP_24 = 27,	// Multi-Time Aggregation Packet, Type B (MTAP-24)
	_FR_FU_A = 28,			// Fragmentation unit (FU-A)
	// _FR_FU_B = 29,		// Fragmentation unit (FU-B)
};

enum {
	_NALU_IDR = 5,
	_NALU_SPS = 7,
	_NALU_PPS = 8,
};


static int _update_sequence_for_new_packet(us_rtpc_s *rtpc, u16 seq);
static ldf _rtp_to_local_ts(us_rtpc_s *rtpc, u32 rts);
static void _update_timestamps_for_new_packet(us_rtpc_s *rtpc, u32 rts);

static void _unwrapping_begin(us_rtpc_s *rtpc);
static void _unwrapping_end(us_rtpc_s *rtpc);
static void _unwrap_whole(us_rtpc_s *rtpc, const u8 *data, u16 size);


us_rtpc_s *us_rtpc_init(us_frame_callback_f callback, void *user_data) {
	us_rtpc_s *rtpc;
	US_CALLOC(rtpc, 1);
	rtpc->user_data = user_data;
	rtpc->frame = us_frame_init();
	rtpc->callback = callback;
	return rtpc;
}

void us_rtpc_destroy(us_rtpc_s *rtpc) {
	us_frame_destroy(rtpc->frame);
	free(rtpc);
}

us_rtpc_unwrap_result_e us_rtpc_unwrap(
	us_rtpc_s *rtpc,
	const u8 *payload,
	int payload_size,
	u16 seq,
	u32 rts
) {
	US_A(payload != NULL);
	US_A(payload_size >= 1);

	if (_update_sequence_for_new_packet(rtpc, seq) < 0) {
		return US_RUR_INVALID_SEQ;
	}
	_update_timestamps_for_new_packet(rtpc, rts);

	rtpc->had_packets = true;

	const u8 fragment_type = payload[0] & 0x1F;

	if (rtpc->fu && (rtpc->fu_rts != rts || fragment_type != _FR_FU_A)) {
		rtpc->fu = false;
		if (!rtpc->fu_is_bad) { // Not reported yet
			rtpc->skip_until_key = true;
			return US_RUR_DEPACKETIZATION_FAILED_RETRY;
		}
	}

	if (fragment_type >= _FR_REGULAR_NAL_MIN && fragment_type <= _FR_REGULAR_NAL_MAX) {
		_unwrap_whole(rtpc, payload, payload_size);

	} else if (fragment_type == _FR_STAP_A) { // Single Time Aggregation Packet, Type A (STAP-A).
		const u8 *aggregated_unit = payload + 1; // skip aggregation header (1 byte)
		payload_size -= 1;

		while (payload_size > 0) {
			if (payload_size < 2) { // Aggregated NAL unit size (2 bytes) is missing
				US_LOG_ERROR("Aggregated NALU size is missing: seq=%u, rts=%u", seq, rts);
				return US_RUR_BAD_PACKET;
			}

			const u16 nalu_size = ntohs(*((const u16 *)aggregated_unit));
			aggregated_unit	+= 2;
			payload_size -= 2;

			if (nalu_size == 0) {
				US_LOG_ERROR("Aggregated NALU size is zero: seq=%u, rts=%u", seq, rts);
				return US_RUR_BAD_PACKET;
			}

			if (payload_size < nalu_size) { // aggregated unit size declared as bigger than available data size
				US_LOG_ERROR("Aggregated NALU size is too big: seq=%u, rts=%u", seq, rts);
				return US_RUR_BAD_PACKET;
			}

			_unwrap_whole(rtpc, aggregated_unit, nalu_size);

			aggregated_unit	+= nalu_size;
			payload_size -= nalu_size;
		}

	} else if (fragment_type == _FR_FU_A) { // Fragmentation unit (FU-A).
		if (payload_size < 2) { // FU header is missing (2 bytes)
			US_LOG_ERROR("FU header is missing: seq=%u, rts=%u", seq, rts);
			return US_RUR_BAD_PACKET;
		}

		const u8 fu_indicator = payload[0];
		const u8 fu_header = payload[1];
		const bool first_fragment = !!(fu_header & 0x80);
		const bool last_fragment = !!(fu_header & 0x40);

		if (first_fragment && last_fragment) { // should never happen according to RFC
			return US_RUR_BAD_PACKET;
		}

		if (rtpc->fu && rtpc->fu_is_bad) {
			return US_RUR_DEPACKETIZATION_SKIPPED;
		}

		if (!rtpc->fu) {
			rtpc->fu = true;
			rtpc->fu_seq = seq;
			rtpc->fu_rts = rts;

			if (!first_fragment) {
				// US_LOG_INFO("Ignoring FU without a first fragment: seq=%u, rts=%u", seq, rts);
				rtpc->fu_is_bad = true;
				rtpc->skip_until_key = true;
				return US_RUR_DEPACKETIZATION_FAILED;
			}
			rtpc->fu_is_bad = false;

			const u8 header = (fu_indicator & 0xE0) | (fu_header & 0x1F);
			_unwrapping_begin(rtpc);
			us_frame_append_data(rtpc->frame, &header, sizeof(header));
			// - FU indicator - FU header
			us_frame_append_data(rtpc->frame, (const u8 *)payload + 2, payload_size - 2);

		} else if (!first_fragment && (seq == rtpc->fu_seq + 1) && (rts == rtpc->fu_rts)) {
			//       ^^^ Already had first fragment
			rtpc->fu_seq = seq;

			// - FU indicator - FU header
			us_frame_append_data(rtpc->frame, (const u8 *)payload + 2, payload_size - 2);

			if (last_fragment) {
				_unwrapping_end(rtpc);
				rtpc->fu = false;
			}

		} else {
			// US_LOG_ERROR("Something went wrong with FU processing: seq=%u, rts=%u", seq, rts);
			rtpc->fu_is_bad = true;
			rtpc->skip_until_key = true;
			return US_RUR_DEPACKETIZATION_FAILED;
		}

	} else {
		US_LOG_ERROR("Unsupported video unit type=%x: seq=%u, rts=%u", fragment_type, seq, rts);
		return US_RUR_UNSUPPORTED_UNIT_TYPE;
	}
	return US_RUR_SUCCESS;
}

void us_rtpc_sync_timestamp(us_rtpc_s *rtpc, ldf ntp_ts, u32 packet_rts) {
	if (!rtpc->had_packets) {
		return;
	}
	if (rtpc->reference_ntp_ts > ntp_ts) {
		return;
	}

	if (rtpc->reference_ntp_ts == 0.L) {
		if (rtpc->reference_packet_rts < packet_rts) {
			const ldf diff = (packet_rts - rtpc->reference_packet_rts) / 90000.L;
			rtpc->reference_local_ts += diff;
			rtpc->reference_packet_rts = packet_rts;
			rtpc->reference_ntp_ts = ntp_ts;
		}
	} else {
		const ldf diff = ntp_ts - rtpc->reference_ntp_ts;
		rtpc->reference_local_ts += diff;
		rtpc->reference_packet_rts = packet_rts;
		rtpc->reference_ntp_ts = ntp_ts;
	}
}

static int _update_sequence_for_new_packet(us_rtpc_s *rtpc, u16 seq) {
	if (
		rtpc->had_packets
		&& (
			rtpc->last_packet_seq == seq
			|| (rtpc->last_packet_seq < seq && seq - rtpc->last_packet_seq > (UINT16_MAX >> 1))
			|| (seq < rtpc->last_packet_seq && rtpc->last_packet_seq - seq < (UINT16_MAX >> 1))
		)
	) {
		return -1;
	}
	rtpc->last_packet_seq = seq;
	return 0;
}

static ldf _rtp_to_local_ts(us_rtpc_s *rtpc, u32 rts) {
	if (rtpc->reference_packet_rts <= rts) {
		return rtpc->reference_local_ts + (rts - rtpc->reference_packet_rts) / 90000.L;
	} else {
		return rtpc->reference_local_ts - (rtpc->reference_packet_rts - rts) / 90000.L;
	}
}

static void _update_timestamps_for_new_packet(us_rtpc_s *rtpc, u32 rts) {
	const ldf now_ts = us_get_now_monotonic();
	ldf ts; // Fill for any goto

	if (!rtpc->had_packets) {
		ts = _rtp_to_local_ts(rtpc, rts); // Used in packet_update
		goto full_update;
	}
	if (rtpc->last_packet_rts != rts) { // If this is a new frame
		ts = _rtp_to_local_ts(rtpc, rts);
		if (rtpc->last_packet_rts > rts || fabsl(now_ts - ts) > .1L) {
			// ^ RTP timestamp wrapped |OR| ^ Too significant mistake
			goto full_update;
		}
		goto packet_update;
	}

	return;

full_update:
	rtpc->reference_local_ts = now_ts;
	rtpc->reference_packet_rts = rts;
	rtpc->reference_ntp_ts = 0.L; // Force resync
	// US_LOG_INFO("Reference timestamps reseted");
packet_update:
	rtpc->last_packet_rts = rts;
	rtpc->frame_ts = ts;
}

static void _unwrapping_begin(us_rtpc_s *rtpc) {
	us_frame_s *const frame = rtpc->frame;
	u8 *const data = frame->data;
	const uz allocated = frame->allocated;

	// Reset the content, start from scratch
	memset(frame, 0, sizeof(us_frame_s));
	frame->data = data;
	frame->allocated = allocated;

	static const u8 nalu_start_code[] = {0, 0, 0, 1};
	us_frame_set_data(rtpc->frame, nalu_start_code, sizeof(nalu_start_code));
}

static void _unwrapping_end(us_rtpc_s *rtpc) {
	us_frame_s *const frame = rtpc->frame;

	US_A(frame->used > 4); // NAL unit start code

	const u8 nalu_type = (frame->data[4] & 0x1F);
	if (rtpc->skip_until_key && nalu_type != _NALU_SPS && nalu_type != _NALU_PPS) {
		if (nalu_type != _NALU_IDR) {
			frame->used = 0;
			return;
		} else {
			rtpc->skip_until_key = false;
		}
	}

	frame->format = V4L2_PIX_FMT_H264;
	frame->width = rtpc->frame_width;
	frame->height = rtpc->frame_height;
	frame->grab_begin_ts = rtpc->frame_ts;
	frame->grab_end_ts = rtpc->frame_ts;

	// Exchange frames
	rtpc->frame = rtpc->callback(frame, rtpc->user_data);
	US_A(rtpc->frame != NULL);
}

static void _unwrap_whole(us_rtpc_s *rtpc, const u8 *data, u16 size) {
	US_A(size >= 1);

	const u8 nalu_type = (data[0] & 0x1F);
	if (nalu_type == _NALU_SPS) {
		uint width = 0;
		uint height = 0;
		if (
			!us_h264_parse_sps_nalu(data, size, &width, &height)
			&& (rtpc->frame_width != width || rtpc->frame_height != height)
		) {
			US_LOG_INFO("Frame size updated: width=%u, height=%u", width, height);
			rtpc->frame_width = width;
			rtpc->frame_height = height;
		}
	}

	_unwrapping_begin(rtpc);
	us_frame_append_data(rtpc->frame, data, size);
	_unwrapping_end(rtpc);
}
