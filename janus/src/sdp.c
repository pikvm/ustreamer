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


#include "sdp.h"

#include <inttypes.h>

#include <janus/plugins/plugin.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"

#include "rtp.h"
#include "rtpv.h"
#include "rtpa.h"


char *us_sdp_create(u32 video_ssrc, u32 audio_ssrc, bool acap, bool aplay) {
	char *video_sdp;
	{
		// https://tools.ietf.org/html/rfc6184
		// https://github.com/meetecho/janus-gateway/issues/2443
		const uint pl = US_RTP_H264_PAYLOAD;
		US_ASPRINTF(
			video_sdp,
			"m=video 1 RTP/SAVPF %u" RN
			"c=IN IP4 0.0.0.0" RN
			"a=rtpmap:%u H264/90000" RN
			"a=fmtp:%u profile-level-id=42E01F;packetization-mode=1" RN
			"a=rtcp-fb:%u ccm fir" RN
			"a=rtcp-fb:%u nack" RN
			"a=rtcp-fb:%u nack pli" RN
			"a=rtcp-fb:%u goog-remb" RN
			"a=mid:v" RN
			"a=msid:video v" RN
			"a=ssrc:%" PRIu32 " cname:ustreamer" RN
			"a=extmap:1/sendonly urn:3gpp:video-orientation" RN
			"a=extmap:2/sendonly http://www.webrtc.org/experiments/rtp-hdrext/playout-delay" RN
			"a=extmap:3/sendonly http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time" RN
			"a=sendonly" RN,
			pl, pl, pl, pl, pl, pl, pl,
			video_ssrc);
	}

	char *audio_sdp;
	if (acap || aplay) {
		const uint pl = US_RTP_OPUS_PAYLOAD;

		char *dir = "";
		if (acap && aplay) {
			dir = "sendrecv";
		} else if (acap && !aplay) {
			dir = "sendonly";
		} else if (!acap && aplay) {
			dir = "recvonly";
		}

		US_ASPRINTF(
			audio_sdp,
			"m=audio 1 RTP/SAVPF %u" RN
			"c=IN IP4 0.0.0.0" RN
			"a=rtpmap:%u OPUS/%u/%u" RN
			"a=fmtp:%u sprop-stereo=1" RN // useinbandfec=1
			"a=mid:a" RN
			"a=msid:audio a" RN
			"a=ssrc:%" PRIu32 " cname:ustreamer" RN
			"a=%s" RN,
			pl, pl,
			US_RTP_OPUS_HZ, US_RTP_OPUS_CH,
			pl,
			audio_ssrc,
			dir);
	} else {
		audio_sdp = us_strdup("");
	}

	char *sdp;
	US_ASPRINTF(sdp,
		"v=0" RN
		"o=- %" PRIu64 " 1 IN IP4 0.0.0.0" RN
		"s=PiKVM uStreamer" RN
		"t=0 0" RN
		"%s%s",
		us_get_now_id() >> 1,
#		if JANUS_PLUGIN_API_VERSION >= 100
		// Place video SDP before audio SDP so that the video and audio streams
		// have predictable indices, even if audio is not available.
		// See also client.c.
		video_sdp, audio_sdp
#		else
		// For versions of Janus prior to 1.x, place the audio SDP first.
		audio_sdp, video_sdp
#		endif
	);

	free(audio_sdp);
	free(video_sdp);
	return sdp;
}
