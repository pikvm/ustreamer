/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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


typedef struct {
	encoder_s	*enc;
	hw_buffer_s	*hw;
	char		*dest_role;
	frame_s		*dest;
} _job_s;


static workers_pool_s *_stream_init_loop(stream_s *stream);
static workers_pool_s *_stream_init_one(stream_s *stream);
static bool _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps);

static void *_worker_job_init(worker_s *wr, void *v_arg);
static void _worker_job_destroy(void *v_job);
static bool _worker_run_job(worker_s *wr);

#ifdef WITH_OMX
static h264_stream_s *_h264_stream_init(memsink_s *sink);
static void _h264_stream_destroy(h264_stream_s *h264);
static void _h264_stream_process(h264_stream_s *h264, const frame_s *frame);
#endif


stream_s *stream_init(device_s *dev, encoder_s *enc) {
	process_s *proc;
	A_CALLOC(proc, 1);
	atomic_init(&proc->stop, false);
	atomic_init(&proc->slowdown, false);

	video_s *video;
	A_CALLOC(video, 1);
	video->frame = frame_init("stream_video");
	atomic_init(&video->updated, false);
	A_MUTEX_INIT(&video->mutex);

	stream_s *stream;
	A_CALLOC(stream, 1);
	stream->last_as_blank = -1;
	stream->error_delay = 1;
	stream->proc = proc;
	stream->video = video;
	stream->dev = dev;
	stream->enc = enc;
	return stream;
}

void stream_destroy(stream_s *stream) {
	A_MUTEX_DESTROY(&stream->video->mutex);
	frame_destroy(stream->video->frame);
	free(stream->video);
	free(stream->proc);
	free(stream);
}

void stream_loop(stream_s *stream) {
	LOG_INFO("Using V4L2 device: %s", stream->dev->path);
	LOG_INFO("Using desired FPS: %u", stream->dev->desired_fps);

#	ifdef WITH_OMX
	if (stream->h264_sink) {
		stream->h264 = _h264_stream_init(stream->h264_sink);
	}
#	endif

	for (workers_pool_s *pool; (pool = _stream_init_loop(stream)) != NULL;) {
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;

		LOG_INFO("Capturing ...");

		while (!atomic_load(&stream->proc->stop)) {
			SEP_DEBUG('-');
			LOG_DEBUG("Waiting for worker ...");

			worker_s *ready_wr = workers_pool_wait(pool);
			_job_s *ready_job = (_job_s *)(ready_wr->job);

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

			if (atomic_load(&stream->proc->stop)) {
				break;
			}

			if (atomic_load(&stream->proc->slowdown)) {
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
	atomic_store(&stream->proc->stop, true);
}

void stream_switch_slowdown(stream_s *stream, bool slowdown) {
	atomic_store(&stream->proc->slowdown, slowdown);
}

static workers_pool_s *_stream_init_loop(stream_s *stream) {

	workers_pool_s *pool = NULL;
	int access_error = 0;

	LOG_DEBUG("%s: stream->proc->stop=%d", __FUNCTION__, atomic_load(&stream->proc->stop));

	while (!atomic_load(&stream->proc->stop)) {
		if (_stream_expose_frame(stream, NULL, 0)) {
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

	encoder_prepare(stream->enc, stream->dev);

#	define DEV(_next) stream->dev->_next
#	define RUN(_next) stream->dev->run->_next
	long double desired_interval = 0;
	if (DEV(desired_fps) > 0 && (DEV(desired_fps) < RUN(hw_fps) || RUN(hw_fps) == 0)) {
		desired_interval = (long double)1 / DEV(desired_fps);
	}
#	undef DEV
#	undef RUN

	return workers_pool_init(
		"jpeg", stream->enc->run->n_workers, desired_interval,
		_worker_job_init, (void *)stream,
		_worker_job_destroy,
		_worker_run_job);

	error:
		device_close(stream->dev);
		return NULL;
}

static bool _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps) {
#	define VID(_next) stream->video->_next

	frame_s *new = NULL;
	bool changed = false;

	A_MUTEX_LOCK(&VID(mutex));

	if (frame) {
		new = frame;
		VID(last_as_blank_ts) = 0; // Останавливаем таймер
		LOG_DEBUG("Exposed ALIVE video frame");

	} else {
		if (VID(frame->online)) { // Если переходим из online в offline
			if (stream->last_as_blank < 0) { // Если last_as_blank выключен, просто покажем старую картинку
				new = stream->blank;
				LOG_INFO("Changed video frame to BLANK");
			} else if (stream->last_as_blank > 0) { // // Если нужен таймер - запустим
				VID(last_as_blank_ts) = get_now_monotonic() + stream->last_as_blank;
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
			&& VID(last_as_blank_ts) != 0
			&& VID(last_as_blank_ts) < get_now_monotonic()
		) {
			new = stream->blank;
			VID(last_as_blank_ts) = 0; // // Останавливаем таймер
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

static void *_worker_job_init(worker_s *wr, void *v_stream) {
	stream_s *stream = (stream_s *)v_stream;

	_job_s *job;
	A_CALLOC(job, 1);
	job->enc = stream->enc;

	const size_t dest_role_len = strlen(wr->name) + 16;
	A_CALLOC(job->dest_role, dest_role_len);
	snprintf(job->dest_role, dest_role_len, "%s_dest", wr->name);
	job->dest = frame_init(job->dest_role);

	return (void *)job;
}
static void _worker_job_destroy(void *v_job) {
	_job_s *job = (_job_s *)v_job;
	frame_destroy(job->dest);
	free(job->dest_role);
	free(job);
}

static bool _worker_run_job(worker_s *wr) {
	_job_s *job = (_job_s *)wr->job;

	LOG_DEBUG("Worker %s compressing JPEG from buffer %u ...", wr->name, job->hw->buf_info.index);
	bool ok = !encoder_compress(job->enc, wr->number, &job->hw->raw, job->dest);
	if (ok) {
		LOG_VERBOSE("Compressed new JPEG: size=%zu, time=%0.3Lf, worker=%s, buffer=%u",
			job->dest->used,
			job->dest->encode_end_ts - job->dest->encode_begin_ts,
			wr->name,
			job->hw->buf_info.index);
	} else {
		LOG_VERBOSE("Compression failed: worker=%s, buffer=%u", wr->name, job->hw->buf_info.index);
	}
	return ok;
}

#ifdef WITH_OMX
static h264_stream_s *_h264_stream_init(memsink_s *sink) {
	h264_stream_s *h264;
	A_CALLOC(h264, 1);

	if ((h264->enc = h264_encoder_init()) == NULL) {
		goto error;
	}

	h264->dest = frame_init("h264_dest");
	h264->sink = sink;

	return h264;

	error:
		_h264_stream_destroy(h264);
		return NULL;
}

static void _h264_stream_destroy(h264_stream_s *h264) {
	if (h264->enc) {
		h264_encoder_destroy(h264->enc);
	}
	if (h264->dest) {
		frame_destroy(h264->dest);
	}
	free(h264);
}

static void _h264_stream_process(h264_stream_s *h264, const frame_s *frame) {
#	define NEQ(_field) (frame->_field != h264->enc->run->_field)
	if (NEQ(width) || NEQ(height) || NEQ(format)) {
#	undef NEQ
		h264_encoder_prepare(h264->enc, frame->width, frame->height, frame->format);
	}
	if (h264->enc->run->format) {
		if (h264_encoder_compress(h264->enc, frame, h264->dest) == 0) {
			memsink_server_put(h264->sink, h264->dest);
		}
	}
}
#endif
