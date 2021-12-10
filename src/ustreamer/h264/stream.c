/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
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


#include "stream.h"


h264_stream_s *h264_stream_init(memsink_s *sink, const char *path, unsigned bitrate, unsigned gop) {
	h264_stream_s *h264;
	A_CALLOC(h264, 1);
	h264->sink = sink;
	h264->tmp_src = frame_init();
	h264->dest = frame_init();
	atomic_init(&h264->online, false);
	h264->enc = m2m_h264_encoder_init("H264", path, bitrate, gop);
	return h264;
}

void h264_stream_destroy(h264_stream_s *h264) {
	m2m_encoder_destroy(h264->enc);
	frame_destroy(h264->dest);
	frame_destroy(h264->tmp_src);
	free(h264);
}

void h264_stream_process(h264_stream_s *h264, const frame_s *frame, bool force_key) {
	if (!memsink_server_check(h264->sink, frame)) {
		return;
	}

	if (is_jpeg(frame->format)) {
		long double now = get_now_monotonic();
		LOG_DEBUG("H264: Input frame is JPEG; decoding ...");
		if (unjpeg(frame, h264->tmp_src, true) < 0) {
			return;
		}
		frame = h264->tmp_src;
		LOG_VERBOSE("H264: JPEG decoded; time=%.3Lf", get_now_monotonic() - now);
	}

	bool online = false;
	if (!m2m_encoder_ensure_ready(h264->enc, frame)) {
		if (!m2m_encoder_compress(h264->enc, frame, h264->dest, force_key)) {
			online = !memsink_server_put(h264->sink, h264->dest);
		}
	}
	atomic_store(&h264->online, online);
}
