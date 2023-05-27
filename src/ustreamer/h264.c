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


#include "h264.h"


us_h264_stream_s *us_h264_stream_init(us_memsink_s *sink, const char *path, unsigned bitrate, unsigned gop) {
	us_h264_stream_s *h264;
	US_CALLOC(h264, 1);
	h264->sink = sink;
	h264->tmp_src = us_frame_init();
	h264->dest = us_frame_init();
	atomic_init(&h264->online, false);
	h264->enc = us_m2m_h264_encoder_init("H264", path, bitrate, gop);
	return h264;
}

void us_h264_stream_destroy(us_h264_stream_s *h264) {
	us_m2m_encoder_destroy(h264->enc);
	us_frame_destroy(h264->dest);
	us_frame_destroy(h264->tmp_src);
	free(h264);
}

void us_h264_stream_process(us_h264_stream_s *h264, const us_frame_s *frame, bool force_key) {
	if (!us_memsink_server_check(h264->sink, frame)) {
		return;
	}

	if (us_is_jpeg(frame->format)) {
		const long double now = us_get_now_monotonic();
		US_LOG_DEBUG("H264: Input frame is JPEG; decoding ...");
		if (us_unjpeg(frame, h264->tmp_src, true) < 0) {
			return;
		}
		frame = h264->tmp_src;
		US_LOG_VERBOSE("H264: JPEG decoded; time=%.3Lf", us_get_now_monotonic() - now);
	}

	if (h264->key_requested) {
		US_LOG_INFO("H264: Requested keyframe by a sink client");
		h264->key_requested = false;
		force_key = true;
	}

	bool online = false;
	if (!us_m2m_encoder_compress(h264->enc, frame, h264->dest, force_key)) {
		online = !us_memsink_server_put(h264->sink, h264->dest, &h264->key_requested);
	}
	atomic_store(&h264->online, online);
}
