/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file is partially based on this code:                       #
#      - https://github.com/catid/kvm/blob/master/kvm_pipeline/src           #
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


#include "rtpv.h"

#include <stdlib.h>

#include <linux/videodev2.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"
#include "uslibs/frame.h"

#include "rtp.h"


static sz _find_annexb(const u8 *data, uz size);
static bool _is_nalu_small(uz size);

static void _rtpv_process_nalu(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool first_nalu, bool last_nalu);
static void _rtpv_process_nalu_small(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool first_nalu, bool last_nalu);
static void _rtpv_process_nalu_big(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool first_nalu, bool last_nalu);


us_rtpv_s *us_rtpv_init(us_rtp_callback_f callback) {
	us_rtpv_s *rtpv;
	US_CALLOC(rtpv, 1);
	rtpv->rtp = us_rtp_init();
	us_rtp_assign(rtpv->rtp, US_RTP_H264_PAYLOAD, true);
	rtpv->callback = callback;
	return rtpv;
}

void us_rtpv_destroy(us_rtpv_s *rtpv) {
	us_rtp_destroy(rtpv->rtp);
	free(rtpv);
}

#define _PRE 3 // Annex B prefix length

void us_rtpv_wrap(us_rtpv_s *rtpv, const us_frame_s *frame, bool zero_playout_delay) {
	US_A(frame->format == V4L2_PIX_FMT_H264);

	if (frame->used <= _PRE) {
		return;
	}

	rtpv->rtp->zero_playout_delay = zero_playout_delay;
	rtpv->rtp->grab_ntp_ts = us_get_now_ntp() - us_ld_to_ntp(us_get_now_monotonic() - frame->grab_begin_ts);

	const u32 pts = us_get_now_monotonic_u64() * 9 / 100; // PTS units are in 90 kHz

	// Bytestream can be like:
	//   [00-]00-00-01 NALU [00-]00-00-01 NALU
	// ... with optional 00 prefix for _PRE.
	// We need to iterate by NALUs only and throw away separators.

	const sz offset_to_first = _find_annexb(frame->data, frame->used);
	if (offset_to_first < 0) {
		return;
	}
	uz begin = offset_to_first + _PRE;

	bool first_nalu = true;
	while (begin < frame->used) { // Find and iterate by NALUs
		const u8 *const data = frame->data + begin;
		const sz offset_to_next = _find_annexb(data, frame->used - begin);

		if (offset_to_next >= 0) { // Process NALUs between prefixes
			uz size = offset_to_next;
			if (size > 1 && data[size - 1] == 0) { // Check for extra trailing zero
				--size;
			}
			if (size > 1) { // Skip too short NALUs
				_rtpv_process_nalu(rtpv, data, size, pts, first_nalu, false);
				first_nalu = false;
			}
			begin += offset_to_next + _PRE;

		} else { // Process the tail
			const uz size = frame->used - begin;
			if (size > 1) {
				_rtpv_process_nalu(rtpv, data, size, pts, first_nalu, true);
				first_nalu = false;
			}
			break;
		}
	}
}

static sz _find_annexb(const u8 *data, uz size) {
	// Parses buffer for 00 00 01 start codes
	if (size >= _PRE) {
		for (uz i = 0; i <= size - _PRE; ++i) {
			if (
				data[i] == 0
				&& data[i + 1] == 0
				&& data[i + 2] == 1
			) {
				return i;
			}
		}
	}
	return -1;
}

#undef _PRE

static bool _is_nalu_small(uz size) {
	return (size + US_RTP_HEADER_SIZE <= US_RTP_TOTAL_SIZE);
}

static void _rtpv_process_nalu(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool first_nalu, bool last_nalu) {
	US_A(size > 1);
	if (_is_nalu_small(size)) {
		_rtpv_process_nalu_small(rtpv, data, size, pts, first_nalu, last_nalu);
	} else {
		_rtpv_process_nalu_big(rtpv, data, size, pts, first_nalu, last_nalu);
	}
}

static void _rtpv_process_nalu_small(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool first_nalu, bool last_nalu) {
	US_A(size > 1);
	US_A(_is_nalu_small(size));

	us_rtp_s *rtp = rtpv->rtp;
	u8 *dg = rtp->datagram;

	us_rtp_write_header(rtp, pts, last_nalu);
	memcpy(dg + US_RTP_HEADER_SIZE, data, size);
	rtp->used = size + US_RTP_HEADER_SIZE;
	rtp->first_of_frame = first_nalu;
	rtp->last_of_frame = last_nalu;
	rtpv->callback(rtp);
}

static void _rtpv_process_nalu_big(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool first_nalu, bool last_nalu) {
	US_A(size > 1);
	US_A(!_is_nalu_small(size));

	const uint ref_idc = (data[0] >> 5) & 3;
	const uint type = data[0] & 0x1F;
	us_rtp_s *rtp = rtpv->rtp;
	u8 *dg = rtp->datagram;

	const uz fu_overhead = US_RTP_HEADER_SIZE + 2; // FU-A overhead

	const u8 *src = data + 1;
	sz remaining = size - 1;

	bool first = true;
	while (remaining > 0) {
		sz frag_size = US_RTP_TOTAL_SIZE - fu_overhead;
		const bool last = (remaining <= frag_size);
		if (last) {
			frag_size = remaining;
		}

		us_rtp_write_header(rtp, pts, (last_nalu && last));

		dg[US_RTP_HEADER_SIZE] = 28 | (ref_idc << 5);

		u8 fu = type;
		if (first) {
			fu |= 0x80;
		}
		if (last) {
			fu |= 0x40;
		}
		dg[US_RTP_HEADER_SIZE + 1] = fu;

		memcpy(dg + fu_overhead, src, frag_size);
		rtp->used = fu_overhead + frag_size;
		rtp->first_of_frame = (first_nalu && first);
		rtp->last_of_frame = (last_nalu && last);
		rtpv->callback(rtp);

		src += frag_size;
		remaining -= frag_size;
		first = false;
	}
}
