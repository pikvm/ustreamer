/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
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

#	define OPTION(_required, _key, _value) {#_key, _required, V4L2_CID_MPEG_VIDEO_##_key, _value}

	m2m_option_s options[] = {
		OPTION(true, BITRATE, bitrate * 1000),
		OPTION(false, BITRATE_PEAK, bitrate * 1000),
		OPTION(true, H264_I_PERIOD, gop),
		OPTION(true, H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE),
		OPTION(true, H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0),
		OPTION(true, REPEAT_SEQ_HEADER, 1),
		OPTION(false, H264_MIN_QP, 16),
		OPTION(false, H264_MAX_QP, 32),
		{NULL, false, 0, 0},
	};

#	undef OPTION

	// FIXME: 30 or 0? https://github.com/6by9/yavta/blob/master/yavta.c#L2100
	// По логике вещей правильно 0, но почему-то на низких разрешениях типа 640x480
	// енкодер через несколько секунд перестает производить корректные фреймы.
	// TODO: Это было актуально для MMAL, надо проверить для V4L2.

	h264->enc = m2m_encoder_init("H264", path, V4L2_PIX_FMT_H264, 30, options);
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
