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


static workers_pool_s *_stream_init_loop(stream_s *stream);
static workers_pool_s *_stream_init_one(stream_s *stream);
static bool _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps);

#ifdef WITH_OMX
static h264_stream_s *_h264_stream_init(memsink_s *sink, unsigned bitrate, unsigned gop);
static void _h264_stream_destroy(h264_stream_s *h264);
static void _h264_stream_process(h264_stream_s *h264, const frame_s *frame);
#endif


#define RUN(_next) stream->run->_next


stream_s *stream_init(device_s *dev, encoder_s *enc) {
	stream_runtime_s *run;
	A_CALLOC(run, 1);
	atomic_init(&run->stop, false);
	atomic_init(&run->slowdown, false);

	video_s *video;
	A_CALLOC(video, 1);
	video->frame = frame_init("stream_video");
	atomic_init(&video->updated, false);
	A_MUTEX_INIT(&video->mutex);
	run->video = video;

	stream_s *stream;
	A_CALLOC(stream, 1);
	stream->dev = dev;
	stream->enc = enc;
	stream->last_as_blank = -1;
	stream->error_delay = 1;
#	ifdef WITH_OMX
	stream->h264_bitrate = 5000; // Kbps
	stream->h264_gop = 30;
#	endif
	stream->run = run;
	return stream;
}

void stream_destroy(stream_s *stream) {
	A_MUTEX_DESTROY(&RUN(video->mutex));
	frame_destroy(RUN(video->frame));
	free(RUN(video));
	free(stream->run);
	free(stream);
}

void stream_loop(stream_s *stream) {
	assert(stream->blank);

	LOG_INFO("Using V4L2 device: %s", stream->dev->path);
	LOG_INFO("Using desired FPS: %u", stream->dev->desired_fps);

#	ifdef WITH_OMX
	if (stream->h264_sink) {
		stream->h264 = _h264_stream_init(stream->h264_sink, stream->h264_bitrate, stream->h264_gop);
	}
#	endif

	for (workers_pool_s *pool; (pool = _stream_init_loop(stream)) != NULL;) {
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;

		LOG_INFO("Capturing ...");

		while (!atomic_load(&RUN(stop))) {
			SEP_DEBUG('-');
			LOG_DEBUG("Waiting for worker ...");

			worker_s *ready_wr = workers_pool_wait(pool);
			encoder_job_s *ready_job = (encoder_job_s *)(ready_wr->job);

			if (ready_job->hw) {
				if (device_release_buffer(stream->dev, ready_job->hw) < 0) {
					ready_wr->job_failed = true;
				}
				ready_job->hw = NULL;

				if (!ready_wr->job_failed) {
					if (ready_wr->job_timely) {
						_stream_expose_frame(stream, ready_job->dest, captured_fps);
						if (stream->jpeg_sink) {
							memsink_server_put(stream->jpeg_sink, ready_job->dest);
						}
						LOG_PERF("##### Encoded frame exposed; worker=%s", ready_wr->name);
					} else {
						LOG_PERF("----- Encoded frame dropped; worker=%s", ready_wr->name);
					}
				} else {
					break;
				}
			}

			if (atomic_load(&RUN(stop))) {
				break;
			}

			if (atomic_load(&RUN(slowdown))) {
				usleep(1000000);
			}

			bool has_read;
			bool has_write;
			bool has_error;
			int selected = device_select(stream->dev, &has_read, &has_write, &has_error);

			if (selected < 0) {
				if (errno != EINTR) {
					LOG_PERROR("Mainloop select() error");
					break;
				}
			} else if (selected == 0) { // Persistent timeout
#				ifdef WITH_GPIO
				gpio_set_stream_online(false);
#				endif
			} else {
				if (has_read) {
					LOG_DEBUG("Frame is ready");

#					ifdef WITH_GPIO
					gpio_set_stream_online(true);
#					endif

					const long double now = get_now_monotonic();
					const long long now_second = floor_ms(now);

					hw_buffer_s *hw;
					int buf_index = device_grab_buffer(stream->dev, &hw);

					if (buf_index >= 0) {
						if (now < grab_after) {
							fluency_passed += 1;
							LOG_VERBOSE("Passed %u frames for fluency: now=%.03Lf, grab_after=%.03Lf",
								fluency_passed, now, grab_after);
							if (device_release_buffer(stream->dev, hw) < 0) {
								break;
							}
						} else {
							fluency_passed = 0;

							if (now_second != captured_fps_second) {
								captured_fps = captured_fps_accum;
								captured_fps_accum = 0;
								captured_fps_second = now_second;
								LOG_PERF_FPS("A new second has come; captured_fps=%u", captured_fps);
							}
							captured_fps_accum += 1;

							const long double fluency_delay = workers_pool_get_fluency_delay(pool, ready_wr);
							grab_after = now + fluency_delay;
							LOG_VERBOSE("Fluency: delay=%.03Lf, grab_after=%.03Lf", fluency_delay, grab_after);

							ready_job->hw = hw;
							workers_pool_assign(pool, ready_wr);
							LOG_DEBUG("Assigned new frame in buffer %d to worker %s", buf_index, ready_wr->name);

#							ifdef WITH_OMX
							if (stream->h264) {
								_h264_stream_process(stream->h264, &hw->raw);
							}
#							endif
						}
					} else if (buf_index != -2) { // -2 for broken frame
						break;
					}
				}

				if (has_write) {
					LOG_ERROR("Got unexpected writing event, seems device was disconnected");
					break;
				}

				if (has_error) {
					LOG_INFO("Got V4L2 event");
					if (device_consume_event(stream->dev) < 0) {
						break;
					}
				}
			}
		}

		workers_pool_destroy(pool);
		device_switch_capturing(stream->dev, false);
		device_close(stream->dev);

#		ifdef WITH_GPIO
		gpio_set_stream_online(false);
#		endif
	}

#	ifdef WITH_OMX
	if (stream->h264) {
		_h264_stream_destroy(stream->h264);
	}
#	endif
}

void stream_loop_break(stream_s *stream) {
	atomic_store(&RUN(stop), true);
}

void stream_switch_slowdown(stream_s *stream, bool slowdown) {
	atomic_store(&RUN(slowdown), slowdown);
}

static workers_pool_s *_stream_init_loop(stream_s *stream) {

	workers_pool_s *pool = NULL;
	int access_error = 0;

	LOG_DEBUG("%s: stream->run->stop=%d", __FUNCTION__, atomic_load(&RUN(stop)));

	while (!atomic_load(&RUN(stop))) {
		if (_stream_expose_frame(stream, NULL, 0)) {
			if (stream->jpeg_sink) {
				memsink_server_put(stream->jpeg_sink, stream->blank);
			}
#			ifdef WITH_OMX
			if (stream->h264) {
				_h264_stream_process(stream->h264, stream->blank);
			}
#			endif
		}

		if (access(stream->dev->path, R_OK|W_OK) < 0) {
			if (access_error != errno) {
				SEP_INFO('=');
				LOG_PERROR("Can't access device");
				LOG_INFO("Waiting for the device access ...");
				access_error = errno;
			}
			sleep(stream->error_delay);
			continue;
		} else {
			SEP_INFO('=');
			access_error = 0;
		}

		if ((pool = _stream_init_one(stream)) == NULL) {
			LOG_INFO("Sleeping %u seconds before new stream init ...", stream->error_delay);
			sleep(stream->error_delay);
		} else {
			break;
		}
	}
	return pool;
}

static workers_pool_s *_stream_init_one(stream_s *stream) {
	if (device_open(stream->dev) < 0) {
		goto error;
	}
	if (device_switch_capturing(stream->dev, true) < 0) {
		goto error;
	}
	return encoder_workers_pool_init(stream->enc, stream->dev);
	error:
		device_close(stream->dev);
		return NULL;
}

static bool _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps) {
#	define VID(_next) RUN(video->_next)

	frame_s *new = NULL;
	bool changed = false;

	A_MUTEX_LOCK(&VID(mutex));

	if (frame) {
		new = frame;
		RUN(last_as_blank_ts) = 0; // Останавливаем таймер
		LOG_DEBUG("Exposed ALIVE video frame");

	} else {
		if (VID(frame->online)) { // Если переходим из online в offline
			if (stream->last_as_blank < 0) { // Если last_as_blank выключен, просто покажем старую картинку
				new = stream->blank;
				LOG_INFO("Changed video frame to BLANK");
			} else if (stream->last_as_blank > 0) { // // Если нужен таймер - запустим
				RUN(last_as_blank_ts) = get_now_monotonic() + stream->last_as_blank;
				LOG_INFO("Freezed last ALIVE video frame for %d seconds", stream->last_as_blank);
			} else {  // last_as_blank == 0 - показываем последний фрейм вечно
				LOG_INFO("Freezed last ALIVE video frame forever");
			}
		} else if (stream->last_as_blank < 0) {
			new = stream->blank;
			// LOG_INFO("Changed video frame to BLANK");
		}

		if ( // Если уже оффлайн, включена фича last_as_blank с таймером и он запущен
			stream->last_as_blank > 0
			&& RUN(last_as_blank_ts) != 0
			&& RUN(last_as_blank_ts) < get_now_monotonic()
		) {
			new = stream->blank;
			RUN(last_as_blank_ts) = 0; // // Останавливаем таймер
			LOG_INFO("Changed last ALIVE video frame to BLANK");
		}
	}

	if (new) {
		frame_copy(new, VID(frame));
		changed = true;
	} else if (VID(frame->used) == 0) { // Инициализация
		frame_copy(stream->blank, VID(frame));
		frame = NULL;
		changed = true;
	}
	VID(frame->online) = frame;
	VID(captured_fps) = captured_fps;
	atomic_store(&VID(updated), true);
	A_MUTEX_UNLOCK(&VID(mutex));
	return changed;

#	undef VID
}

#ifdef WITH_OMX
static h264_stream_s *_h264_stream_init(memsink_s *sink, unsigned bitrate, unsigned gop) {
	h264_stream_s *h264;
	A_CALLOC(h264, 1);
	h264->sink = sink;
	h264->tmp_src = frame_init("h264_tmp_src");
	h264->dest = frame_init("h264_dest");

	// FIXME: 30 or 0? https://github.com/6by9/yavta/blob/master/yavta.c#L210
	if ((h264->enc = h264_encoder_init(bitrate, gop, 0)) == NULL) {
		goto error;
	}

	return h264;

	error:
		_h264_stream_destroy(h264);
		return NULL;
}

static void _h264_stream_destroy(h264_stream_s *h264) {
	if (h264->enc) {
		h264_encoder_destroy(h264->enc);
	}
	frame_destroy(h264->dest);
	frame_destroy(h264->tmp_src);
	free(h264);
}

static void _h264_stream_process(h264_stream_s *h264, const frame_s *frame) {
	long double now = get_now_monotonic();
	if (is_jpeg(frame->format)) {
		LOG_DEBUG("H264: Input frame is JPEG; decoding ...");
		if (unjpeg(frame, h264->tmp_src, true) < 0) {
			return;
		}
		LOG_VERBOSE("H264: JPEG decoded; time=%.3Lf", get_now_monotonic() - now);
	} else {
		LOG_DEBUG("H264: Copying source to tmp buffer ...");
		frame_copy(frame, h264->tmp_src);
		LOG_VERBOSE("H264: Source copied; time=%.3Lf", get_now_monotonic() - now);
	}

	if (!h264_encoder_is_prepared_for(h264->enc, h264->tmp_src)) {
		h264_encoder_prepare(h264->enc, h264->tmp_src);
	}

	if (h264->enc->ready) {
		if (h264_encoder_compress(h264->enc, h264->tmp_src, h264->dest) == 0) {
			memsink_server_put(h264->sink, h264->dest);
		}
	}
}
#endif

#undef RUN
