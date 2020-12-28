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


typedef struct _worker_sx {
	pthread_t		tid;
	unsigned		number;
	atomic_bool		*proc_stop;
	atomic_bool		*workers_stop;

	long double		last_comp_time;
	char			*frame_role;
	frame_s			*frame;

	pthread_mutex_t	has_job_mutex;
	unsigned		buf_index;
	atomic_bool		has_job;
	bool			job_timely;
	bool			job_failed;
	long double		job_start_ts;
	pthread_cond_t	has_job_cond;

	pthread_mutex_t	*free_workers_mutex;
	unsigned		*free_workers;
	pthread_cond_t	*free_workers_cond;

	struct _worker_sx *order_prev;
	struct _worker_sx *order_next;

	stream_s		*stream;
} _worker_s;

typedef struct {
	unsigned		n_workers;
	_worker_s		*workers;
	_worker_s		*oldest_wr;
	_worker_s		*latest_wr;

	long double		approx_comp_time;

	pthread_mutex_t	free_workers_mutex;
	unsigned		free_workers;
	pthread_cond_t	free_workers_cond;

	atomic_bool		workers_stop;

	long double		desired_frames_interval;
} _pool_s;


static _pool_s *_stream_init_loop(stream_s *stream);
static _pool_s *_stream_init_one(stream_s *stream);
static bool _stream_expose_frame(stream_s *stream, frame_s *frame, unsigned captured_fps);

static _pool_s *_workers_pool_init(stream_s *stream);
static void _workers_pool_destroy(_pool_s *pool);

static void *_worker_thread(void *v_worker);

static _worker_s *_workers_pool_wait(_pool_s *pool);
static void _workers_pool_assign(_pool_s *pool, _worker_s *ready_wr, unsigned buf_index);
static long double _workers_pool_get_fluency_delay(_pool_s *pool, _worker_s *ready_wr);


stream_s *stream_init(device_s *dev, encoder_s *encoder) {
	process_s *proc;
	video_s *video;
	stream_s *stream;

	A_CALLOC(proc, 1);
	atomic_init(&proc->stop, false);
	atomic_init(&proc->slowdown, false);

	A_CALLOC(video, 1);
	video->frame = frame_init("stream_video");
	atomic_init(&video->updated, false);
	A_MUTEX_INIT(&video->mutex);

	A_CALLOC(stream, 1);
	stream->last_as_blank = -1;
	stream->error_delay = 1;
	stream->proc = proc;
	stream->video = video;
	stream->dev = dev;
	stream->encoder = encoder;
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
#	define DEV(_next) stream->dev->_next
	_pool_s *pool;

	LOG_INFO("Using V4L2 device: %s", DEV(path));
	LOG_INFO("Using desired FPS: %u", DEV(desired_fps));

	while ((pool = _stream_init_loop(stream)) != NULL) {
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;

		LOG_INFO("Capturing ...");

		while (!atomic_load(&stream->proc->stop)) {
			_worker_s *ready_wr;

			SEP_DEBUG('-');
			LOG_DEBUG("Waiting for worker ...");

			ready_wr = _workers_pool_wait(pool);

			if (!ready_wr->job_failed) {
				if (ready_wr->job_timely) {
					_stream_expose_frame(stream, ready_wr->frame, captured_fps);
					LOG_PERF("##### Encoded frame exposed; worker=%u", ready_wr->number);
				} else {
					LOG_PERF("----- Encoded frame dropped; worker=%u", ready_wr->number);
				}
			} else {
				break;
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

					long double now = get_now_monotonic();
					long long now_second = floor_ms(now);

					int buf_index = device_grab_buffer(stream->dev);
					if (buf_index >= 0) {
						if (now < grab_after) {
							fluency_passed += 1;
							LOG_VERBOSE("Passed %u frames for fluency: now=%.03Lf, grab_after=%.03Lf",
								fluency_passed, now, grab_after);
							if (device_release_buffer(stream->dev, buf_index) < 0) {
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

							long double fluency_delay = _workers_pool_get_fluency_delay(pool, ready_wr);

							grab_after = now + fluency_delay;
							LOG_VERBOSE("Fluency: delay=%.03Lf, grab_after=%.03Lf", fluency_delay, grab_after);

#							ifdef WITH_RAWSINK
							if (stream->rawsink && rawsink_server_put(stream->rawsink, &DEV(run->hw_buffers[buf_index].raw)) < 0) {
								stream->rawsink = NULL;
								LOG_ERROR("RAW sink completely disabled due error");
							}
#							endif

							_workers_pool_assign(pool, ready_wr, buf_index);
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

		_workers_pool_destroy(pool);
		device_switch_capturing(stream->dev, false);
		device_close(stream->dev);

#		ifdef WITH_GPIO
		gpio_set_stream_online(false);
#		endif
	}

#	undef DEV
}

void stream_loop_break(stream_s *stream) {
	atomic_store(&stream->proc->stop, true);
}

void stream_switch_slowdown(stream_s *stream, bool slowdown) {
	atomic_store(&stream->proc->slowdown, slowdown);
}

static _pool_s *_stream_init_loop(stream_s *stream) {
	_pool_s *pool = NULL;
	int access_error = 0;

	LOG_DEBUG("%s: stream->proc->stop=%d", __FUNCTION__, atomic_load(&stream->proc->stop));

	while (!atomic_load(&stream->proc->stop)) {
		if (_stream_expose_frame(stream, NULL, 0)) {
#			ifdef WITH_RAWSINK
			if (stream->rawsink && rawsink_server_put(stream->rawsink, stream->blank) < 0) {
				stream->rawsink = NULL;
				LOG_ERROR("RAW sink completely disabled due error");
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

static _pool_s *_stream_init_one(stream_s *stream) {
	if (device_open(stream->dev) < 0) {
		goto error;
	}
	if (device_switch_capturing(stream->dev, true) < 0) {
		goto error;
	}

	encoder_prepare(stream->encoder, stream->dev);
	return _workers_pool_init(stream);

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
		if (VID(online)) { // Если переходим из online в offline
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
	VID(online) = frame;
	VID(captured_fps) = captured_fps;
	atomic_store(&VID(updated), true);
	A_MUTEX_UNLOCK(&VID(mutex));
	return changed;

#	undef VID
}

static _pool_s *_workers_pool_init(stream_s *stream) {
#	define DEV(_next) stream->dev->_next
#	define RUN(_next) stream->dev->run->_next

	_pool_s *pool;

	LOG_INFO("Creating pool with %u workers ...", stream->encoder->run->n_workers);

	A_CALLOC(pool, 1);

	pool->n_workers = stream->encoder->run->n_workers;
	A_CALLOC(pool->workers, pool->n_workers);

	A_MUTEX_INIT(&pool->free_workers_mutex);
	A_COND_INIT(&pool->free_workers_cond);

	atomic_init(&pool->workers_stop, false);

	if (DEV(desired_fps) > 0 && (DEV(desired_fps) < RUN(hw_fps) || RUN(hw_fps) == 0)) {
		pool->desired_frames_interval = (long double)1 / DEV(desired_fps);
	}

#	undef RUN
#	undef DEV

	for (unsigned number = 0; number < pool->n_workers; ++number) {
#		define WR(_next) pool->workers[number]._next

		A_CALLOC(WR(frame_role), 32);
		sprintf(WR(frame_role), "worker_dest_%u", number);
		WR(frame) = frame_init(WR(frame_role));

		A_MUTEX_INIT(&WR(has_job_mutex));
		atomic_init(&WR(has_job), false);
		A_COND_INIT(&WR(has_job_cond));

		WR(number) = number;
		WR(proc_stop) = &stream->proc->stop;
		WR(workers_stop) = &pool->workers_stop;

		WR(free_workers_mutex) = &pool->free_workers_mutex;
		WR(free_workers) = &pool->free_workers;
		WR(free_workers_cond) = &pool->free_workers_cond;

		WR(stream) = stream;

		A_THREAD_CREATE(&WR(tid), _worker_thread, (void *)&(pool->workers[number]));

		pool->free_workers += 1;

#		undef WR
	}
	return pool;
}

static void _workers_pool_destroy(_pool_s *pool) {
	LOG_INFO("Destroying workers pool ...");

	atomic_store(&pool->workers_stop, true);
	for (unsigned number = 0; number < pool->n_workers; ++number) {
#		define WR(_next) pool->workers[number]._next

		A_MUTEX_LOCK(&WR(has_job_mutex));
		atomic_store(&WR(has_job), true); // Final job: die
		A_MUTEX_UNLOCK(&WR(has_job_mutex));
		A_COND_SIGNAL(&WR(has_job_cond));

		A_THREAD_JOIN(WR(tid));
		A_MUTEX_DESTROY(&WR(has_job_mutex));
		A_COND_DESTROY(&WR(has_job_cond));

		frame_destroy(WR(frame));
		free(WR(frame_role));

#		undef WR
	}

	A_MUTEX_DESTROY(&pool->free_workers_mutex);
	A_COND_DESTROY(&pool->free_workers_cond);

	free(pool->workers);
	free(pool);
}

static void *_worker_thread(void *v_worker) {
	_worker_s *wr = (_worker_s *)v_worker;

	A_THREAD_RENAME("worker-%u", wr->number);
	LOG_DEBUG("Hello! I am a worker #%u ^_^", wr->number);

	while (!atomic_load(wr->proc_stop) && !atomic_load(wr->workers_stop)) {
		LOG_DEBUG("Worker %u waiting for a new job ...", wr->number);

		A_MUTEX_LOCK(&wr->has_job_mutex);
		A_COND_WAIT_TRUE(atomic_load(&wr->has_job), &wr->has_job_cond, &wr->has_job_mutex);
		A_MUTEX_UNLOCK(&wr->has_job_mutex);

		if (!atomic_load(wr->workers_stop)) {
#			define PIC(_next) wr->frame->_next

			LOG_DEBUG("Worker %u compressing JPEG from buffer %u ...", wr->number, wr->buf_index);

			wr->job_failed = (bool)encoder_compress(
				wr->stream->encoder,
				wr->number,
				&wr->stream->dev->run->hw_buffers[wr->buf_index].raw,
				wr->frame
			);

			if (device_release_buffer(wr->stream->dev, wr->buf_index) == 0) {
				if (!wr->job_failed) {
					wr->job_start_ts = PIC(encode_begin_ts);
					wr->last_comp_time = PIC(encode_end_ts) - wr->job_start_ts;

					LOG_VERBOSE("Compressed new JPEG: size=%zu, time=%0.3Lf, worker=%u, buffer=%u",
						PIC(used), wr->last_comp_time, wr->number, wr->buf_index);
				} else {
					LOG_VERBOSE("Compression failed: worker=%u, buffer=%u", wr->number, wr->buf_index);
				}
			} else {
				wr->job_failed = true;
			}

			atomic_store(&wr->has_job, false);

#			undef PIC
		}

		A_MUTEX_LOCK(wr->free_workers_mutex);
		*wr->free_workers += 1;
		A_MUTEX_UNLOCK(wr->free_workers_mutex);
		A_COND_SIGNAL(wr->free_workers_cond);
	}

	LOG_DEBUG("Bye-bye (worker %u)", wr->number);
	return NULL;
}

static _worker_s *_workers_pool_wait(_pool_s *pool) {
	_worker_s *ready_wr = NULL;

	A_MUTEX_LOCK(&pool->free_workers_mutex);
	A_COND_WAIT_TRUE(pool->free_workers, &pool->free_workers_cond, &pool->free_workers_mutex);
	A_MUTEX_UNLOCK(&pool->free_workers_mutex);

	if (pool->oldest_wr && !atomic_load(&pool->oldest_wr->has_job)) {
		ready_wr = pool->oldest_wr;
		ready_wr->job_timely = true;
		pool->oldest_wr = pool->oldest_wr->order_next;
	} else {
		for (unsigned number = 0; number < pool->n_workers; ++number) {
			if (
				!atomic_load(&pool->workers[number].has_job) && (
					ready_wr == NULL
					|| ready_wr->job_start_ts < pool->workers[number].job_start_ts
				)
			) {
				ready_wr = &pool->workers[number];
				break;
			}
		}
		assert(ready_wr != NULL);
		ready_wr->job_timely = false; // Освободился воркер, получивший задание позже (или самый первый при самом первом захвате)
	}
	return ready_wr;
}

static void _workers_pool_assign(_pool_s *pool, _worker_s *ready_wr, unsigned buf_index) {
	if (pool->oldest_wr == NULL) {
		pool->oldest_wr = ready_wr;
		pool->latest_wr = pool->oldest_wr;
	} else {
		if (ready_wr->order_next) {
			ready_wr->order_next->order_prev = ready_wr->order_prev;
		}
		if (ready_wr->order_prev) {
			ready_wr->order_prev->order_next = ready_wr->order_next;
		}
		ready_wr->order_prev = pool->latest_wr;
		pool->latest_wr->order_next = ready_wr;
		pool->latest_wr = ready_wr;
	}
	pool->latest_wr->order_next = NULL;

	A_MUTEX_LOCK(&ready_wr->has_job_mutex);
	ready_wr->buf_index = buf_index;
	atomic_store(&ready_wr->has_job, true);
	A_MUTEX_UNLOCK(&ready_wr->has_job_mutex);
	A_COND_SIGNAL(&ready_wr->has_job_cond);

	A_MUTEX_LOCK(&pool->free_workers_mutex);
	pool->free_workers -= 1;
	A_MUTEX_UNLOCK(&pool->free_workers_mutex);

	LOG_DEBUG("Assigned new frame in buffer %u to worker %u", buf_index, ready_wr->number);
}

static long double _workers_pool_get_fluency_delay(_pool_s *pool, _worker_s *ready_wr) {
	long double approx_comp_time;
	long double min_delay;

	approx_comp_time = pool->approx_comp_time * 0.9 + ready_wr->last_comp_time * 0.1;

	LOG_VERBOSE("Correcting approx_comp_time: %.3Lf -> %.3Lf (last_comp_time=%.3Lf)",
		pool->approx_comp_time, approx_comp_time, ready_wr->last_comp_time);

	pool->approx_comp_time = approx_comp_time;

	min_delay = pool->approx_comp_time / pool->n_workers; // Среднее время работы размазывается на N воркеров

	if (pool->desired_frames_interval > 0 && min_delay > 0 && pool->desired_frames_interval > min_delay) {
		// Искусственное время задержки на основе желаемого FPS, если включен --desired-fps
		// и аппаратный fps не попадает точно в желаемое значение
		return pool->desired_frames_interval;
	}
	return min_delay;
}
