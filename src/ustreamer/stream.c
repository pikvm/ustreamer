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


struct _worker_t {
	pthread_t			tid;
	unsigned			number;
	atomic_bool			*proc_stop;
	atomic_bool			*workers_stop;

	long double			last_comp_time;
	struct picture_t	*picture;

	pthread_mutex_t		has_job_mutex;
	unsigned			buf_index;
	atomic_bool			has_job;
	bool				job_timely;
	bool				job_failed;
	long double			job_start_ts;
	pthread_cond_t		has_job_cond;

	pthread_mutex_t		*free_workers_mutex;
	unsigned			*free_workers;
	pthread_cond_t		*free_workers_cond;

	struct _worker_t	*order_prev;
	struct _worker_t	*order_next;

	struct stream_t		*stream;
};

struct _workers_pool_t {
	unsigned			n_workers;
	struct _worker_t	*workers;
	struct _worker_t	*oldest_wr;
	struct _worker_t	*latest_wr;

	long double			approx_comp_time;

	pthread_mutex_t		free_workers_mutex;
	unsigned			free_workers;
	pthread_cond_t		free_workers_cond;

	atomic_bool			workers_stop;

	long double			desired_frames_interval;
};


static struct _workers_pool_t *_stream_init_loop(struct stream_t *stream);
static struct _workers_pool_t *_stream_init_one(struct stream_t *stream);
static void _stream_expose_picture(struct stream_t *stream, struct picture_t *picture, unsigned captured_fps);

static struct _workers_pool_t *_workers_pool_init(struct stream_t *stream);
static void _workers_pool_destroy(struct _workers_pool_t *pool);

static void *_worker_thread(void *v_worker);

static struct _worker_t *_workers_pool_wait(struct _workers_pool_t *pool);
static void _workers_pool_assign(struct _workers_pool_t *pool, struct _worker_t *ready_wr, unsigned buf_index);
static long double _workers_pool_get_fluency_delay(struct _workers_pool_t *pool, struct _worker_t *ready_wr);


struct stream_t *stream_init(struct device_t *dev, struct encoder_t *encoder) {
	struct process_t *proc;
	struct video_t *video;
	struct stream_t *stream;

	A_CALLOC(proc, 1);
	atomic_init(&proc->stop, false);
	atomic_init(&proc->slowdown, false);

	A_CALLOC(video, 1);
	video->picture = picture_init();
	atomic_init(&video->updated, false);
	A_MUTEX_INIT(&video->mutex);

	A_CALLOC(stream, 1);
	stream->error_delay = 1;
#	ifdef WITH_RAWSINK
	stream->rawsink_name = "";
	stream->rawsink_mode = 0660;
#	endif
	stream->proc = proc;
	stream->video = video;
	stream->dev = dev;
	stream->encoder = encoder;
	return stream;
}

void stream_destroy(struct stream_t *stream) {
	A_MUTEX_DESTROY(&stream->video->mutex);
	picture_destroy(stream->video->picture);
	free(stream->video);
	free(stream->proc);
	free(stream);
}

void stream_loop(struct stream_t *stream) {
#	define DEV(_next) stream->dev->_next
	struct _workers_pool_t *pool;

	LOG_INFO("Using V4L2 device: %s", DEV(path));
	LOG_INFO("Using desired FPS: %u", DEV(desired_fps));

#	ifdef WITH_RAWSINK
	struct rawsink_t *rawsink = NULL;
	if (stream->rawsink_name[0] != '\0') {
		rawsink = rawsink_init(stream->rawsink_name, stream->rawsink_mode, stream->rawsink_rm, true);
	}
#	endif

	while ((pool = _stream_init_loop(stream)) != NULL) {
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;

		LOG_INFO("Capturing ...");

		LOG_DEBUG("Pre-allocating memory for stream picture ...");
		picture_realloc_data(stream->video->picture, picture_get_generous_size(DEV(run->width), DEV(run->height)));

		while (!atomic_load(&stream->proc->stop)) {
			struct _worker_t *ready_wr;

			SEP_DEBUG('-');
			LOG_DEBUG("Waiting for worker ...");

			ready_wr = _workers_pool_wait(pool);

			if (!ready_wr->job_failed) {
				if (ready_wr->job_timely) {
					_stream_expose_picture(stream, ready_wr->picture, captured_fps);
					LOG_PERF("##### Encoded picture exposed; worker=%u", ready_wr->number);
				} else {
					LOG_PERF("----- Encoded picture dropped; worker=%u", ready_wr->number);
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
							if (rawsink) {
								rawsink_put(
									rawsink,
									DEV(run->hw_buffers[buf_index].data),
									DEV(run->hw_buffers[buf_index].used),
									DEV(run->hw_buffers[buf_index].format),
									DEV(run->hw_buffers[buf_index].width),
									DEV(run->hw_buffers[buf_index].height),
									DEV(run->hw_buffers[buf_index].grab_ts)
								);
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

		A_MUTEX_LOCK(&stream->video->mutex);
		stream->video->online = false;
		atomic_store(&stream->video->updated, true);
		A_MUTEX_UNLOCK(&stream->video->mutex);

		_workers_pool_destroy(pool);
		device_switch_capturing(stream->dev, false);
		device_close(stream->dev);

#		ifdef WITH_GPIO
		gpio_set_stream_online(false);
#		endif
	}

#	ifdef WITH_RAWSINK
	if (rawsink) {
		rawsink_destroy(rawsink);
	}
#	endif

#	undef DEV
}

void stream_loop_break(struct stream_t *stream) {
	atomic_store(&stream->proc->stop, true);
}

void stream_switch_slowdown(struct stream_t *stream, bool slowdown) {
	atomic_store(&stream->proc->slowdown, slowdown);
}

static struct _workers_pool_t *_stream_init_loop(struct stream_t *stream) {
	struct _workers_pool_t *pool = NULL;
	int access_error = 0;

	LOG_DEBUG("%s: stream->proc->stop=%d", __FUNCTION__, atomic_load(&stream->proc->stop));

	while (!atomic_load(&stream->proc->stop)) {
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

static struct _workers_pool_t *_stream_init_one(struct stream_t *stream) {
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

static void _stream_expose_picture(struct stream_t *stream, struct picture_t *picture, unsigned captured_fps) {
	A_MUTEX_LOCK(&stream->video->mutex);

	picture_copy(picture, stream->video->picture);

	stream->video->online = true;
	stream->video->captured_fps = captured_fps;
	atomic_store(&stream->video->updated, true);

	A_MUTEX_UNLOCK(&stream->video->mutex);
}

static struct _workers_pool_t *_workers_pool_init(struct stream_t *stream) {
#	define DEV(_next) stream->dev->_next
#	define RUN(_next) stream->dev->run->_next

	struct _workers_pool_t *pool;
	size_t picture_size = picture_get_generous_size(RUN(width), RUN(height));

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

		WR(picture) = picture_init();
		picture_realloc_data(WR(picture), picture_size);

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

static void _workers_pool_destroy(struct _workers_pool_t *pool) {
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

		picture_destroy(WR(picture));

#		undef WR
	}

	A_MUTEX_DESTROY(&pool->free_workers_mutex);
	A_COND_DESTROY(&pool->free_workers_cond);

	free(pool->workers);
	free(pool);
}

static void *_worker_thread(void *v_worker) {
	struct _worker_t *wr = (struct _worker_t *)v_worker;

	A_THREAD_RENAME("worker-%u", wr->number);
	LOG_DEBUG("Hello! I am a worker #%u ^_^", wr->number);

	while (!atomic_load(wr->proc_stop) && !atomic_load(wr->workers_stop)) {
		LOG_DEBUG("Worker %u waiting for a new job ...", wr->number);

		A_MUTEX_LOCK(&wr->has_job_mutex);
		A_COND_WAIT_TRUE(atomic_load(&wr->has_job), &wr->has_job_cond, &wr->has_job_mutex);
		A_MUTEX_UNLOCK(&wr->has_job_mutex);

		if (!atomic_load(wr->workers_stop)) {
#			define PIC(_next) wr->picture->_next

			LOG_DEBUG("Worker %u compressing JPEG from buffer %u ...", wr->number, wr->buf_index);

			wr->job_failed = (bool)encoder_compress_buffer(
				wr->stream->encoder,
				wr->number,
				&wr->stream->dev->run->hw_buffers[wr->buf_index],
				wr->picture
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

static struct _worker_t *_workers_pool_wait(struct _workers_pool_t *pool) {
	struct _worker_t *ready_wr = NULL;

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

static void _workers_pool_assign(struct _workers_pool_t *pool, struct _worker_t *ready_wr, unsigned buf_index) {
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

static long double _workers_pool_get_fluency_delay(struct _workers_pool_t *pool, struct _worker_t *ready_wr) {
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
