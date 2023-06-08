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


#include "stream.h"


static us_workers_pool_s *_stream_init_loop(us_stream_s *stream);
static us_workers_pool_s *_stream_init_one(us_stream_s *stream);
static void _stream_expose_frame(us_stream_s *stream, us_frame_s *frame, unsigned captured_fps);


#define _RUN(x_next) stream->run->x_next

#define _SINK_PUT(x_sink, x_frame) { \
		if (stream->x_sink && us_memsink_server_check(stream->x_sink, x_frame)) {\
			bool m_key_requested; /* Unused */ \
			us_memsink_server_put(stream->x_sink, x_frame, &m_key_requested); \
		} \
	}

#define _H264_PUT(x_frame, x_force_key) { \
		if (_RUN(h264)) { \
			us_h264_stream_process(_RUN(h264), x_frame, x_force_key); \
		} \
	}


us_stream_s *us_stream_init(us_device_s *dev, us_encoder_s *enc) {
	us_stream_runtime_s *run;
	US_CALLOC(run, 1);
	atomic_init(&run->stop, false);

	us_video_s *video;
	US_CALLOC(video, 1);
	video->frame = us_frame_init();
	atomic_init(&video->updated, false);
	US_MUTEX_INIT(video->mutex);
	atomic_init(&video->has_clients, false);
	run->video = video;

	us_stream_s *stream;
	US_CALLOC(stream, 1);
	stream->dev = dev;
	stream->enc = enc;
	stream->last_as_blank = -1;
	stream->error_delay = 1;
	stream->h264_bitrate = 5000; // Kbps
	stream->h264_gop = 30;
	stream->run = run;
	return stream;
}

void us_stream_destroy(us_stream_s *stream) {
	US_MUTEX_DESTROY(_RUN(video->mutex));
	us_frame_destroy(_RUN(video->frame));
	free(_RUN(video));
	free(stream->run);
	free(stream);
}

void us_stream_loop(us_stream_s *stream) {
	assert(stream->blank != NULL);

	US_LOG_INFO("Using V4L2 device: %s", stream->dev->path);
	US_LOG_INFO("Using desired FPS: %u", stream->dev->desired_fps);

	if (stream->h264_sink != NULL) {
		_RUN(h264) = us_h264_stream_init(stream->h264_sink, stream->h264_m2m_path, stream->h264_bitrate, stream->h264_gop);
	}

	for (us_workers_pool_s *pool; (pool = _stream_init_loop(stream)) != NULL;) {
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;

		US_LOG_INFO("Capturing ...");

		while (!atomic_load(&_RUN(stop))) {
			US_SEP_DEBUG('-');
			US_LOG_DEBUG("Waiting for worker ...");

			us_worker_s *const ready_wr = us_workers_pool_wait(pool);
			us_encoder_job_s *const ready_job = (us_encoder_job_s *)(ready_wr->job);

			if (ready_job->hw != NULL) {
				if (us_device_release_buffer(stream->dev, ready_job->hw) < 0) {
					ready_wr->job_failed = true;
				}
				ready_job->hw = NULL;

				if (!ready_wr->job_failed) {
					if (ready_wr->job_timely) {
						_stream_expose_frame(stream, ready_job->dest, captured_fps);
						US_LOG_PERF("##### Encoded frame exposed; worker=%s", ready_wr->name);
					} else {
						US_LOG_PERF("----- Encoded frame dropped; worker=%s", ready_wr->name);
					}
				} else {
					break;
				}
			}

			bool h264_force_key = false;
			if (stream->slowdown) {
				unsigned slc = 0;
				for (; slc < 10 && !atomic_load(&_RUN(stop)) && !us_stream_has_clients(stream); ++slc) {
					usleep(100000);
					++slc;
				}
				h264_force_key = (slc == 10);
			}

			if (atomic_load(&_RUN(stop))) {
				break;
			}

			bool has_read;
			bool has_write;
			bool has_error;
			const int selected = us_device_select(stream->dev, &has_read, &has_write, &has_error);

			if (selected < 0) {
				if (errno != EINTR) {
					US_LOG_PERROR("Mainloop select() error");
					break;
				}
			} else if (selected == 0) { // Persistent timeout
#				ifdef WITH_GPIO
				us_gpio_set_stream_online(false);
#				endif
			} else {
				if (has_read) {
					US_LOG_DEBUG("Frame is ready");

#					ifdef WITH_GPIO
					us_gpio_set_stream_online(true);
#					endif

					const long double now = us_get_now_monotonic();
					const long long now_second = us_floor_ms(now);

					us_hw_buffer_s *hw;
					const int buf_index = us_device_grab_buffer(stream->dev, &hw);

					if (buf_index >= 0) {
						if (now < grab_after) {
							fluency_passed += 1;
							US_LOG_VERBOSE("Passed %u frames for fluency: now=%.03Lf, grab_after=%.03Lf",
								fluency_passed, now, grab_after);
							if (us_device_release_buffer(stream->dev, hw) < 0) {
								break;
							}
						} else {
							fluency_passed = 0;

							if (now_second != captured_fps_second) {
								captured_fps = captured_fps_accum;
								captured_fps_accum = 0;
								captured_fps_second = now_second;
								US_LOG_PERF_FPS("A new second has come; captured_fps=%u", captured_fps);
							}
							captured_fps_accum += 1;

							const long double fluency_delay = us_workers_pool_get_fluency_delay(pool, ready_wr);
							grab_after = now + fluency_delay;
							US_LOG_VERBOSE("Fluency: delay=%.03Lf, grab_after=%.03Lf", fluency_delay, grab_after);

							ready_job->hw = hw;
							us_workers_pool_assign(pool, ready_wr);
							US_LOG_DEBUG("Assigned new frame in buffer=%d to worker=%s", buf_index, ready_wr->name);

							_SINK_PUT(raw_sink, &hw->raw);
							_H264_PUT(&hw->raw, h264_force_key);
						}
					} else if (buf_index != -2) { // -2 for broken frame
						break;
					}
				}

				// Это условие было добавлено из параноидальных соображений, и мы ни разу не сталкивались
				// с подобными ошибками, кроме случая с libcamerify, который генерит эвенты на запись.
				// Судя по всему, игнорирование has_write не делает никому плохо.
				/*if (has_write) {
					US_LOG_ERROR("Got unexpected writing event, seems device was disconnected");
					break;
				}*/

				if (has_error) {
					US_LOG_INFO("Got V4L2 event");
					if (us_device_consume_event(stream->dev) < 0) {
						break;
					}
				}
			}
		}

		us_workers_pool_destroy(pool);
		us_device_switch_capturing(stream->dev, false);
		us_device_close(stream->dev);

#		ifdef WITH_GPIO
		us_gpio_set_stream_online(false);
#		endif
	}

	US_DELETE(_RUN(h264), us_h264_stream_destroy);
}

void us_stream_loop_break(us_stream_s *stream) {
	atomic_store(&_RUN(stop), true);
}

bool us_stream_has_clients(us_stream_s *stream) {
	return (
		atomic_load(&_RUN(video->has_clients))
		// has_clients синков НЕ обновляются в реальном времени
		|| (stream->sink != NULL && atomic_load(&stream->sink->has_clients))
		|| (_RUN(h264) != NULL && /*_RUN(h264->sink) == NULL ||*/ atomic_load(&_RUN(h264->sink->has_clients)))
	);
}

static us_workers_pool_s *_stream_init_loop(us_stream_s *stream) {

	us_workers_pool_s *pool = NULL;
	int access_error = 0;

	US_LOG_DEBUG("%s: stream->run->stop=%d", __FUNCTION__, atomic_load(&_RUN(stop)));

	while (!atomic_load(&_RUN(stop))) {
		_stream_expose_frame(stream, NULL, 0);

		if (access(stream->dev->path, R_OK|W_OK) < 0) {
			if (access_error != errno) {
				US_SEP_INFO('=');
				US_LOG_PERROR("Can't access device");
				US_LOG_INFO("Waiting for the device access ...");
				access_error = errno;
			}
			sleep(stream->error_delay);
			continue;
		} else {
			US_SEP_INFO('=');
			access_error = 0;
		}

		if ((pool = _stream_init_one(stream)) == NULL) {
			US_LOG_INFO("Sleeping %u seconds before new stream init ...", stream->error_delay);
			sleep(stream->error_delay);
		} else {
			break;
		}
	}
	return pool;
}

static us_workers_pool_s *_stream_init_one(us_stream_s *stream) {
	if (us_device_open(stream->dev) < 0) {
		goto error;
	}
	if (
		stream->enc->type == US_ENCODER_TYPE_M2M_VIDEO
		|| stream->enc->type == US_ENCODER_TYPE_M2M_IMAGE
		|| (_RUN(h264) && !us_is_jpeg(stream->dev->run->format))
	) {
		us_device_export_to_dma(stream->dev);
	}
	if (us_device_switch_capturing(stream->dev, true) < 0) {
		goto error;
	}
	return us_encoder_workers_pool_init(stream->enc, stream->dev);
	error:
		us_device_close(stream->dev);
		return NULL;
}

static void _stream_expose_frame(us_stream_s *stream, us_frame_s *frame, unsigned captured_fps) {
#	define VID(x_next) _RUN(video->x_next)

	us_frame_s *new = NULL;

	US_MUTEX_LOCK(VID(mutex));

	if (frame != NULL) {
		new = frame;
		_RUN(last_as_blank_ts) = 0; // Останавливаем таймер
		US_LOG_DEBUG("Exposed ALIVE video frame");

	} else {
		if (VID(frame->used == 0)) {
			new = stream->blank; // Инициализация
			_RUN(last_as_blank_ts) = 0;

		} else if (VID(frame->online)) { // Если переходим из online в offline
			if (stream->last_as_blank < 0) { // Если last_as_blank выключен, просто покажем старую картинку
				new = stream->blank;
				US_LOG_INFO("Changed video frame to BLANK");
			} else if (stream->last_as_blank > 0) { // // Если нужен таймер - запустим
				_RUN(last_as_blank_ts) = us_get_now_monotonic() + stream->last_as_blank;
				US_LOG_INFO("Freezed last ALIVE video frame for %d seconds", stream->last_as_blank);
			} else {  // last_as_blank == 0 - показываем последний фрейм вечно
				US_LOG_INFO("Freezed last ALIVE video frame forever");
			}

		} else if (stream->last_as_blank < 0) {
			new = stream->blank;
			// US_LOG_INFO("Changed video frame to BLANK");
		}

		if ( // Если уже оффлайн, включена фича last_as_blank с таймером и он запущен
			stream->last_as_blank > 0
			&& _RUN(last_as_blank_ts) != 0
			&& _RUN(last_as_blank_ts) < us_get_now_monotonic()
		) {
			new = stream->blank;
			_RUN(last_as_blank_ts) = 0; // // Останавливаем таймер
			US_LOG_INFO("Changed last ALIVE video frame to BLANK");
		}
	}

	if (new != NULL) {
		us_frame_copy(new, VID(frame));
	}
	VID(frame->online) = (frame != NULL);
	VID(captured_fps) = captured_fps;
	atomic_store(&VID(updated), true);

	US_MUTEX_UNLOCK(VID(mutex));

	new = (frame ? frame : stream->blank);
	_SINK_PUT(sink, new);

	if (frame == NULL) {
		_SINK_PUT(raw_sink, stream->blank);
		_H264_PUT(stream->blank, false);
	}

#	undef VID
}
