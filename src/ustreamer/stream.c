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
static void _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps);


#define RUN(_next) stream->run->_next

#define SINK_PUT(_sink, _frame) { \
		if (stream->_sink && memsink_server_check(stream->_sink, _frame)) {\
			memsink_server_put(stream->_sink, _frame); \
		} \
	}

#ifdef WITH_OMX
#	define H264_PUT(_frame, _vcsm_handle, _force_key) { \
			if (RUN(h264)) { \
				h264_stream_process(RUN(h264), _frame, _vcsm_handle, _force_key); \
			} \
		}
#endif


stream_s *stream_init(device_s *dev, encoder_s *enc) {
	stream_runtime_s *run;
	A_CALLOC(run, 1);
	atomic_init(&run->stop, false);

	video_s *video;
	A_CALLOC(video, 1);
	video->frame = frame_init();
	atomic_init(&video->updated, false);
	A_MUTEX_INIT(&video->mutex);
	atomic_init(&video->has_clients, false);
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
		RUN(h264) = h264_stream_init(stream->h264_sink, stream->h264_bitrate, stream->h264_gop);
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
						LOG_PERF("##### Encoded frame exposed; worker=%s", ready_wr->name);
					} else {
						LOG_PERF("----- Encoded frame dropped; worker=%s", ready_wr->name);
					}
				} else {
					break;
				}
			}

#			ifdef WITH_OMX
			bool h264_force_key = false;
#			endif
			if (stream->slowdown) {
				unsigned slc = 0;
				for (; slc < 10 && !atomic_load(&RUN(stop)) && !stream_has_clients(stream); ++slc) {
					usleep(100000);
					++slc;
				}
#				ifdef WITH_OMX
				h264_force_key = (slc == 10);
#				endif
			}

			if (atomic_load(&RUN(stop))) {
				break;
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

							SINK_PUT(raw_sink, &hw->raw);
#							ifdef WITH_OMX
							H264_PUT(&hw->raw, hw->vcsm_handle, h264_force_key);
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
	if (RUN(h264)) {
		h264_stream_destroy(RUN(h264));
	}
#	endif
}

void stream_loop_break(stream_s *stream) {
	atomic_store(&RUN(stop), true);
}

bool stream_has_clients(stream_s *stream) {
	return (
		atomic_load(&RUN(video->has_clients))
		// has_clients синков НЕ обновляются в реальном времени
		|| (stream->sink != NULL && atomic_load(&stream->sink->has_clients))
#		ifdef WITH_OMX
		|| (RUN(h264) != NULL && /*RUN(h264->sink) == NULL ||*/ atomic_load(&RUN(h264->sink->has_clients)))
#		endif
	);
}

static workers_pool_s *_stream_init_loop(stream_s *stream) {

	workers_pool_s *pool = NULL;
	int access_error = 0;

	LOG_DEBUG("%s: stream->run->stop=%d", __FUNCTION__, atomic_load(&RUN(stop)));

	while (!atomic_load(&RUN(stop))) {
		_stream_expose_frame(stream, NULL, 0);

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
#	ifdef WITH_OMX
	if (RUN(h264) && !is_jpeg(stream->dev->run->format)) {
		device_export_to_vcsm(stream->dev);
	}
#	endif
	if (device_switch_capturing(stream->dev, true) < 0) {
		goto error;
	}
	return encoder_workers_pool_init(stream->enc, stream->dev);
	error:
		device_close(stream->dev);
		return NULL;
}

static void _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps) {
#	define VID(_next) RUN(video->_next)

	frame_s *new = NULL;

	A_MUTEX_LOCK(&VID(mutex));

	if (frame) {
		new = frame;
		RUN(last_as_blank_ts) = 0; // Останавливаем таймер
		LOG_DEBUG("Exposed ALIVE video frame");

	} else {
		if (VID(frame->used == 0)) {
			new = stream->blank; // Инициализация
			RUN(last_as_blank_ts) = 0;

		} else if (VID(frame->online)) { // Если переходим из online в offline
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
	}
	VID(frame->online) = (bool)frame;
	VID(captured_fps) = captured_fps;
	atomic_store(&VID(updated), true);

	A_MUTEX_UNLOCK(&VID(mutex));

	new = (frame ? frame : stream->blank);
	SINK_PUT(sink, new);

	if (frame == NULL) {
		SINK_PUT(raw_sink, stream->blank);
#		ifdef WITH_OMX
		H264_PUT(stream->blank, -1, false);
#		endif
	}

#	undef VID
}

#ifdef WITH_OMX
#	undef H264_PUT
#endif
#undef SINK_PUT
#undef RUN
