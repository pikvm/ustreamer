/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file is partially based on this code:                       #
#      - https://github.com/catid/kvm/blob/master/kvm_pipeline/src           #
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


#include "rtpv.h"


void _rtpv_process_nalu(us_rtpv_s *rtpv, const uint8_t *data, size_t size, uint32_t pts, bool marked);

static ssize_t _find_annexb(const uint8_t *data, size_t size);


us_rtpv_s *us_rtpv_init(us_rtp_callback_f callback) {
	us_rtpv_s *rtpv;
	US_CALLOC(rtpv, 1);
	rtpv->rtp = us_rtp_init(96, true);
	rtpv->callback = callback;
	return rtpv;
}

void us_rtpv_destroy(us_rtpv_s *rtpv) {
	us_rtp_destroy(rtpv->rtp);
	free(rtpv);
}

char *us_rtpv_make_sdp(us_rtpv_s *rtpv) {
#	define PAYLOAD rtpv->rtp->payload
	// https://tools.ietf.org/html/rfc6184
	// https://github.com/meetecho/janus-gateway/issues/2443
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
		"a=sendonly" RN,
		PAYLOAD, PAYLOAD, PAYLOAD, PAYLOAD,
		PAYLOAD, PAYLOAD, PAYLOAD,
		rtpv->rtp->ssrc
	);
	return sdp;
#	undef PAYLOAD
}

#define _PRE 3 // Annex B prefix length

void us_rtpv_wrap(us_rtpv_s *rtpv, const us_frame_s *frame) {
	// There is a complicated logic here but everything works as it should:
	//   - https://github.com/pikvm/ustreamer/issues/115#issuecomment-893071775

	assert(frame->format == V4L2_PIX_FMT_H264);

	rtpv->rtp->zero_playout_delay = (frame->gop == 0);

	const uint32_t pts = us_get_now_monotonic_u64() * 9 / 100; // PTS units are in 90 kHz
	ssize_t last_offset = -_PRE;

	while (true) { // Find and iterate by nalus
		const size_t next_start = last_offset + _PRE;
		ssize_t offset = _find_annexb(frame->data + next_start, frame->used - next_start);
		if (offset < 0) {
			break;
		}
		offset += next_start;

		if (last_offset >= 0) {
			const uint8_t *const data = frame->data + last_offset + _PRE;
			size_t size = offset - last_offset - _PRE;
			if (data[size - 1] == 0) { // Check for extra 00
				--size;
			}
			_rtpv_process_nalu(rtpv, data, size, pts, false);
		}

		last_offset = offset;
	}

	if (last_offset >= 0) {
		const uint8_t *const data = frame->data + last_offset + _PRE;
		size_t size = frame->used - last_offset - _PRE;
		_rtpv_process_nalu(rtpv, data, size, pts, true);
	}
}

void _rtpv_process_nalu(us_rtpv_s *rtpv, const uint8_t *data, size_t size, uint32_t pts, bool marked) {
#	define DG rtpv->rtp->datagram

	const unsigned ref_idc = (data[0] >> 5) & 3;
	const unsigned type = data[0] & 0x1F;

	if (size + US_RTP_HEADER_SIZE <= US_RTP_DATAGRAM_SIZE) {
		us_rtp_write_header(rtpv->rtp, pts, marked);
		memcpy(DG + US_RTP_HEADER_SIZE, data, size);
		rtpv->rtp->used = size + US_RTP_HEADER_SIZE;
		rtpv->callback(rtpv->rtp);
		return;
	}

	const size_t fu_overhead = US_RTP_HEADER_SIZE + 2; // FU-A overhead

	const uint8_t *src = data + 1;
	ssize_t remaining = size - 1;

	bool first = true;
	while (remaining > 0) {
		ssize_t frag_size = US_RTP_DATAGRAM_SIZE - fu_overhead;
		const bool last = (remaining <= frag_size);
		if (last) {
			frag_size = remaining;
		}

		us_rtp_write_header(rtpv->rtp, pts, (marked && last));

		DG[US_RTP_HEADER_SIZE] = 28 | (ref_idc << 5);

		uint8_t fu = type;
		if (first) {
			fu |= 0x80;
		}
		if (last) {
			fu |= 0x40;
		}
		DG[US_RTP_HEADER_SIZE + 1] = fu;

		memcpy(DG + fu_overhead, src, frag_size);
		rtpv->rtp->used = fu_overhead + frag_size;
		rtpv->callback(rtpv->rtp);

		src += frag_size;
		remaining -= frag_size;
		first = false;
	}

#	undef DG
}

static ssize_t _find_annexb(const uint8_t *data, size_t size) {
	// Parses buffer for 00 00 01 start codes
	if (size >= _PRE) {
		for (size_t index = 0; index <= size - _PRE; ++index) {
			if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
				return index;
			}
		}
	}
	return -1;
}

#undef _PRE
