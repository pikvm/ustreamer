/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file is partially based on this code:                       #
#      - https://github.com/catid/kvm/blob/master/kvm_pipeline/src           #
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


#include "rtp.h"


rtp_s *rtp_init(unsigned payload, bool video) {
	rtp_s *rtp;
	A_CALLOC(rtp, 1);
	rtp->payload = payload;
	rtp->video = video;
	rtp->ssrc = triple_u32(get_now_monotonic_u64());
	return rtp;
}

rtp_s *rtp_dup(const rtp_s *rtp) {
	rtp_s *new;
	A_CALLOC(new, 1);
	memcpy(new, rtp, sizeof(rtp_s));
	return new;
}

void rtp_destroy(rtp_s *rtp) {
	free(rtp);
}

void rtp_write_header(rtp_s *rtp, uint32_t pts, bool marked) {
	uint32_t word0 = 0x80000000;
	if (marked) {
		word0 |= 1 << 23;
	}
	word0 |= (rtp->payload & 0x7F) << 16;
	word0 |= rtp->seq;
	++rtp->seq;

#	define WRITE_BE_U32(_offset, _value) *((uint32_t *)(rtp->datagram + _offset)) = __builtin_bswap32(_value)
	WRITE_BE_U32(0, word0);
	WRITE_BE_U32(4, pts);
	WRITE_BE_U32(8, rtp->ssrc);
#	undef WRITE_BE_U32
}
