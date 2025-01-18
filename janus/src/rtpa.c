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


#include "rtpa.h"

#include <stdlib.h>
#include <inttypes.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"


us_rtpa_s *us_rtpa_init(us_rtp_callback_f callback) {
	us_rtpa_s *rtpa;
	US_CALLOC(rtpa, 1);
	rtpa->rtp = us_rtp_init();
	us_rtp_assign(rtpa->rtp, US_RTP_OPUS_PAYLOAD, false);
	rtpa->callback = callback;
	return rtpa;
}

void us_rtpa_destroy(us_rtpa_s *rtpa) {
	us_rtp_destroy(rtpa->rtp);
	free(rtpa);
}

char *us_rtpa_make_sdp(us_rtpa_s *rtpa, bool mic) {
	const uint pl = rtpa->rtp->payload;
	char *sdp;
	US_ASPRINTF(sdp,
		"m=audio 1 RTP/SAVPF %u" RN
		"c=IN IP4 0.0.0.0" RN
		"a=rtpmap:%u OPUS/%u/%u" RN
		"a=fmtp:%u sprop-stereo=1" RN // useinbandfec=1
		"a=rtcp-fb:%u nack" RN
		"a=rtcp-fb:%u nack pli" RN
		"a=rtcp-fb:%u goog-remb" RN
		"a=ssrc:%" PRIu32 " cname:ustreamer" RN
		"a=%s" RN,
		pl, pl,
		US_RTP_OPUS_HZ, US_RTP_OPUS_CH,
		pl, pl, pl, pl,
		rtpa->rtp->ssrc,
		(mic ? "sendrecv" : "sendonly")
	);
	return sdp;
}

void us_rtpa_wrap(us_rtpa_s *rtpa, const u8 *data, uz size, u32 pts) {
    if (size + US_RTP_HEADER_SIZE <= US_RTP_DATAGRAM_SIZE) {
        us_rtp_write_header(rtpa->rtp, pts, false);
        memcpy(rtpa->rtp->datagram + US_RTP_HEADER_SIZE, data, size);
		rtpa->rtp->used = size + US_RTP_HEADER_SIZE;
        rtpa->callback(rtpa->rtp);
	}
}
