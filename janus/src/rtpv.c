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
#include <inttypes.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"
#include "uslibs/frame.h"


void _rtpv_process_nalu(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool marked);

static sz _find_annexb(const u8 *data, uz size);


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

char *us_rtpv_make_sdp(us_rtpv_s *rtpv) {
	// https://tools.ietf.org/html/rfc6184
	// https://github.com/meetecho/janus-gateway/issues/2443
	const uint pl = rtpv->rtp->payload;
	char *sdp;
	US_ASPRINTF(sdp,
		"m=video 1 RTP/SAVPF %u" RN
		"c=IN IP4 0.0.0.0" RN
		"a=rtpmap:%u H264/90000" RN
		"a=fmtp:%u profile-level-id=42E01F" RN
		"a=fmtp:%u packetization-mode=1" RN
		"a=rtcp-fb:%u nack" RN
		"a=rtcp-fb:%u nack pli" RN
		"a=rtcp-fb:%u goog-remb" RN
		"a=ssrc:%" PRIu32 " cname:ustreamer" RN
		"a=extmap:1 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay" RN
		"a=extmap:2 urn:3gpp:video-orientation" RN
		"a=sendonly" RN,
		pl, pl, pl, pl,
		pl, pl, pl,
		rtpv->rtp->ssrc
	);
	return sdp;
}

#define _PRE 3 // Annex B prefix length

void us_rtpv_wrap(us_rtpv_s *rtpv, const us_frame_s *frame, bool zero_playout_delay) {
	// There is a complicated logic here but everything works as it should:
	//   - https://github.com/pikvm/ustreamer/issues/115#issuecomment-893071775

	assert(frame->format == V4L2_PIX_FMT_H264);

	rtpv->rtp->zero_playout_delay = zero_playout_delay;

	const u32 pts = us_get_now_monotonic_u64() * 9 / 100; // PTS units are in 90 kHz
	sz last_offset = -_PRE;

	while (true) { // Find and iterate by nalus
		const uz next_start = last_offset + _PRE;
		sz offset = _find_annexb(frame->data + next_start, frame->used - next_start);
		if (offset < 0) {
			break;
		}
		offset += next_start;

		if (last_offset >= 0) {
			const u8 *const data = frame->data + last_offset + _PRE;
			uz size = offset - last_offset - _PRE;
			if (data[size - 1] == 0) { // Check for extra 00
				--size;
			}
			_rtpv_process_nalu(rtpv, data, size, pts, false);
		}

		last_offset = offset;
	}

	if (last_offset >= 0) {
		const u8 *const data = frame->data + last_offset + _PRE;
		uz size = frame->used - last_offset - _PRE;
		_rtpv_process_nalu(rtpv, data, size, pts, true);
	}
}

void _rtpv_process_nalu(us_rtpv_s *rtpv, const u8 *data, uz size, u32 pts, bool marked) {
	const uint ref_idc = (data[0] >> 5) & 3;
	const uint type = data[0] & 0x1F;
	u8 *dg = rtpv->rtp->datagram;

	if (size + US_RTP_HEADER_SIZE <= US_RTP_DATAGRAM_SIZE) {
		us_rtp_write_header(rtpv->rtp, pts, marked);
		memcpy(dg + US_RTP_HEADER_SIZE, data, size);
		rtpv->rtp->used = size + US_RTP_HEADER_SIZE;
		rtpv->callback(rtpv->rtp);
		return;
	}

	const uz fu_overhead = US_RTP_HEADER_SIZE + 2; // FU-A overhead

	const u8 *src = data + 1;
	sz remaining = size - 1;

	bool first = true;
	while (remaining > 0) {
		sz frag_size = US_RTP_DATAGRAM_SIZE - fu_overhead;
		const bool last = (remaining <= frag_size);
		if (last) {
			frag_size = remaining;
		}

		us_rtp_write_header(rtpv->rtp, pts, (marked && last));

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
		rtpv->rtp->used = fu_overhead + frag_size;
		rtpv->callback(rtpv->rtp);

		src += frag_size;
		remaining -= frag_size;
		first = false;
	}
}

static sz _find_annexb(const u8 *data, uz size) {
	// Parses buffer for 00 00 01 start codes
	if (size >= _PRE) {
		for (uz index = 0; index <= size - _PRE; ++index) {
			if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
				return index;
			}
		}
	}
	return -1;
}

#undef _PRE
