/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
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


#include "rtpa.h"


us_rtpa_s *us_rtpa_init(us_rtp_callback_f callback) {
	us_rtpa_s *rtpa;
	US_CALLOC(rtpa, 1);
	rtpa->rtp = us_rtp_init(111, false);
	rtpa->callback = callback;
	return rtpa;
}

void us_rtpa_destroy(us_rtpa_s *rtpa) {
	us_rtp_destroy(rtpa->rtp);
	free(rtpa);
}

char *us_rtpa_make_sdp(us_rtpa_s *rtpa) {
#	define PAYLOAD rtpa->rtp->payload
	char *sdp;
	US_ASPRINTF(sdp,
		"m=audio 1 RTP/SAVPF %u" RN
		"c=IN IP4 0.0.0.0" RN
		"a=rtpmap:%u OPUS/48000/2" RN
		// "a=fmtp:%u useinbandfec=1" RN
		"a=rtcp-fb:%u nack" RN
		"a=rtcp-fb:%u nack pli" RN
		"a=rtcp-fb:%u goog-remb" RN
		"a=ssrc:%" PRIu32 " cname:ustreamer" RN
		"a=sendonly" RN,
		PAYLOAD, PAYLOAD, PAYLOAD, PAYLOAD, PAYLOAD, // PAYLOAD,
		rtpa->rtp->ssrc
	);
#	undef PAYLOAD
	return sdp;
}

void us_rtpa_wrap(us_rtpa_s *rtpa, const uint8_t *data, size_t size, uint32_t pts) {
    if (size + US_RTP_HEADER_SIZE <= US_RTP_DATAGRAM_SIZE) {
        us_rtp_write_header(rtpa->rtp, pts, false);
        memcpy(rtpa->rtp->datagram + US_RTP_HEADER_SIZE, data, size);
		rtpa->rtp->used = size + US_RTP_HEADER_SIZE;
        rtpa->callback(rtpa->rtp);
	}
}
