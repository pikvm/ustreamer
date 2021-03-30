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


h264_stream_s *h264_stream_init(memsink_s *sink, unsigned bitrate, unsigned gop) {
	h264_stream_s *h264;
	A_CALLOC(h264, 1);
	h264->sink = sink;
	h264->tmp_src = frame_init();
	h264->dest = frame_init();
	atomic_init(&h264->online, false);

	// FIXME: 30 or 0? https://github.com/6by9/yavta/blob/master/yavta.c#L2100
	// По логике вещей правильно 0, но почему-то на низких разрешениях типа 640x480
	// енкодер через несколько секунд перестает производить корректные фреймы.
	if ((h264->enc = h264_encoder_init(bitrate, gop, 30)) == NULL) {
		goto error;
	}

	return h264;

	error:
		h264_stream_destroy(h264);
		return NULL;
}

void h264_stream_destroy(h264_stream_s *h264) {
	if (h264->enc) {
		h264_encoder_destroy(h264->enc);
	}
	frame_destroy(h264->dest);
	frame_destroy(h264->tmp_src);
	free(h264);
}

void h264_stream_process(h264_stream_s *h264, const frame_s *frame, int vcsm_handle, bool force_key) {
	if (!memsink_server_check(h264->sink, frame)) {
		return;
	}

	long double now = get_now_monotonic();
	bool zero_copy = false;

	if (is_jpeg(frame->format)) {
		assert(vcsm_handle <= 0);
		LOG_DEBUG("H264: Input frame is JPEG; decoding ...");
		if (unjpeg(frame, h264->tmp_src, true) < 0) {
			return;
		}
		frame = h264->tmp_src;
		LOG_VERBOSE("H264: JPEG decoded; time=%.3Lf", get_now_monotonic() - now);
	} else if (vcsm_handle > 0) {
		LOG_DEBUG("H264: Zero-copy available for the input");
		zero_copy = true;
	} else {
		LOG_DEBUG("H264: Copying source to tmp buffer ...");
		frame_copy(frame, h264->tmp_src);
		frame = h264->tmp_src;
		LOG_VERBOSE("H264: Source copied; time=%.3Lf", get_now_monotonic() - now);
	}

	bool online = false;

	if (!h264_encoder_is_prepared_for(h264->enc, frame, zero_copy)) {
		h264_encoder_prepare(h264->enc, frame, zero_copy);
	}

	if (h264->enc->ready) {
		if (h264_encoder_compress(h264->enc, frame, vcsm_handle, h264->dest, force_key) == 0) {
			online = !memsink_server_put(h264->sink, h264->dest);
		}
	}

	atomic_store(&h264->online, online);
}
