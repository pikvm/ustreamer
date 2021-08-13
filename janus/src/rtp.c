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


#include "rtp.h"


#define PAYLOAD 96 // Payload type
#define PRE 3 // Annex B prefix length


void _rtp_process_nalu(rtp_s *rtp, const uint8_t *data, size_t size, uint32_t pts, bool marked, rtp_callback_f callback);
static void _rtp_write_header(rtp_s *rtp, uint32_t pts, bool marked);

static ssize_t _find_annexb(const uint8_t *data, size_t size);


rtp_s *rtp_init(void) {
	rtp_s *rtp;
	A_CALLOC(rtp, 1);
	rtp->ssrc = triple_u32(get_now_monotonic_u64());
	rtp->sps = frame_init();
	rtp->pps = frame_init();
	A_MUTEX_INIT(&rtp->mutex);
	return rtp;
}

void rtp_destroy(rtp_s *rtp) {
	A_MUTEX_DESTROY(&rtp->mutex);
	frame_destroy(rtp->pps);
	frame_destroy(rtp->sps);
	free(rtp);
}

char *rtp_make_sdp(rtp_s *rtp) {
	A_MUTEX_LOCK(&rtp->mutex);

	if (rtp->sps->used == 0 || rtp->pps->used == 0) {
		A_MUTEX_UNLOCK(&rtp->mutex);
		return NULL;
	}

	char *sps = NULL;
	char *pps = NULL;
	base64_encode(rtp->sps->data, rtp->sps->used, &sps, NULL);
	base64_encode(rtp->pps->data, rtp->pps->used, &pps, NULL);

	A_MUTEX_UNLOCK(&rtp->mutex);

	// https://tools.ietf.org/html/rfc6184
	// https://github.com/meetecho/janus-gateway/issues/2443
	char *sdp;
	A_ASPRINTF(sdp,
		"v=0" RN
		"o=- %" PRIu64 " 1 IN IP4 127.0.0.1" RN
		"s=Pi-KVM uStreamer" RN
		"t=0 0" RN
		"m=video 1 RTP/SAVPF %d" RN
		"c=IN IP4 0.0.0.0" RN
		"a=rtpmap:%d H264/90000" RN
		"a=fmtp:%d profile-level-id=42E01F" RN
		"a=fmtp:%d packetization-mode=1" RN
		"a=fmtp:%d sprop-sps=%s" RN
		"a=fmtp:%d sprop-pps=%s" RN
		"a=rtcp-fb:%d nack" RN
		"a=rtcp-fb:%d nack pli" RN
		"a=rtcp-fb:%d goog-remb" RN
		"a=sendonly" RN,
		get_now_id() >> 1, PAYLOAD, PAYLOAD, PAYLOAD, PAYLOAD,
		PAYLOAD, sps,
		PAYLOAD, pps,
		PAYLOAD, PAYLOAD, PAYLOAD
	);

	free(sps);
	free(pps);
	return sdp;
}

void rtp_wrap_h264(rtp_s *rtp, const frame_s *frame, rtp_callback_f callback) {
	// There is a complicated logic here but everything works as it should:
	//   - https://github.com/pikvm/ustreamer/issues/115#issuecomment-893071775

	assert(frame->format == V4L2_PIX_FMT_H264);

	const uint32_t pts = get_now_monotonic_u64() * 9 / 100; // PTS units are in 90 kHz
	ssize_t last_offset = -PRE;

	while (true) { // Find and iterate by nalus
		const size_t next_start = last_offset + PRE;
		ssize_t offset = _find_annexb(frame->data + next_start, frame->used - next_start);
		if (offset < 0) {
			break;
		}
		offset += next_start;

		if (last_offset >= 0) {
			const uint8_t *data = frame->data + last_offset + PRE;
			size_t size = offset - last_offset - PRE;
			if (data[size - 1] == 0) { // Check for extra 00
				--size;
			}
			_rtp_process_nalu(rtp, data, size, pts, false, callback);
		}

		last_offset = offset;
	}

	if (last_offset >= 0) {
		const uint8_t *data = frame->data + last_offset + PRE;
		size_t size = frame->used - last_offset - PRE;
		_rtp_process_nalu(rtp, data, size, pts, true, callback);
	}
}

void _rtp_process_nalu(rtp_s *rtp, const uint8_t *data, size_t size, uint32_t pts, bool marked, rtp_callback_f callback) {
	const unsigned ref_idc = (data[0] >> 5) & 3;
	const unsigned type = data[0] & 0x1F;

	frame_s *ps = NULL;
	switch (type) {
		case 7: ps = rtp->sps; break;
		case 8: ps = rtp->pps; break;
	}
	if (ps) {
		A_MUTEX_LOCK(&rtp->mutex);
		frame_set_data(ps, data, size);
		A_MUTEX_UNLOCK(&rtp->mutex);
	}

#	define HEADER_SIZE 12

	if (size + HEADER_SIZE <= RTP_DATAGRAM_SIZE) {
		_rtp_write_header(rtp, pts, marked);
		memcpy(rtp->datagram + HEADER_SIZE, data, size);
		callback(rtp->datagram, size + HEADER_SIZE);
		return;
	}

	const size_t fu_overhead = HEADER_SIZE + 2; // FU-A overhead

	const uint8_t *src = data + 1;
	ssize_t remaining = size - 1;

	bool first = true;
	while (remaining > 0) {
		ssize_t frag_size = RTP_DATAGRAM_SIZE - fu_overhead;
		const bool last = (remaining <= frag_size);
		if (last) {
			frag_size = remaining;
		}

		_rtp_write_header(rtp, pts, (marked && last));

		rtp->datagram[HEADER_SIZE] = 28 | (ref_idc << 5);

		uint8_t fu = type;
		if (first) {
			fu |= 0x80;
		}
		if (last) {
			fu |= 0x40;
		}
		rtp->datagram[HEADER_SIZE + 1] = fu;

		memcpy(rtp->datagram + fu_overhead,	src, frag_size);

		callback(rtp->datagram, fu_overhead + frag_size);

		src += frag_size;
		remaining -= frag_size;
		first = false;
	}

#	undef HEADER_SIZE
}

static void _rtp_write_header(rtp_s *rtp, uint32_t pts, bool marked) {
	uint32_t word0 = 0x80000000;
	if (marked) {
		word0 |= 1 << 23;
	}
	word0 |= (PAYLOAD & 0x7F) << 16;
	word0 |= rtp->seq;
	++rtp->seq;

#	define WRITE_BE_U32(_offset, _value) *((uint32_t *)(rtp->datagram + _offset)) = __builtin_bswap32(_value)
	WRITE_BE_U32(0, word0);
	WRITE_BE_U32(4, pts);
	WRITE_BE_U32(8, rtp->ssrc);
#	undef WRITE_BE_U32
}

static ssize_t _find_annexb(const uint8_t *data, size_t size) {
	// Parses buffer for 00 00 01 start codes
	if (size >= PRE) {
		for (size_t index = 0; index <= size - PRE; ++index) {
			if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
				return index;
			}
		}
	}
	return -1;
}

#undef PRE
#undef PAYLOAD
