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

#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "../common/tools.h"
#include "../common/threading.h"
#include "../common/logging.h"

#include "picture.h"
#include "device.h"
#include "encoder.h"
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


struct _worker_t {
	pthread_t			tid;
	unsigned			number;
	atomic_bool			*proc_stop;
	atomic_bool			*workers_stop;

	long double			last_comp_time;

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

	struct device_t		*dev;
	struct encoder_t	*encoder;
};

struct _workers_pool_t {
	unsigned			n_workers;
	struct _worker_t	*workers;
	struct _worker_t	*oldest_worker;
	struct _worker_t	*latest_worker;

	long double			approx_comp_time;

	pthread_mutex_t		free_workers_mutex;
	unsigned			free_workers;
	pthread_cond_t		free_workers_cond;

	atomic_bool			workers_stop;

	long double			desired_frames_interval;
};


static struct _workers_pool_t *_stream_init_loop(struct stream_t *stream);
static struct _workers_pool_t *_stream_init_one(struct stream_t *stream);
static void _stream_expose_picture(struct stream_t *stream, unsigned buf_index, unsigned captured_fps);

static struct _workers_pool_t *_workers_pool_init(struct stream_t *stream);
static void _workers_pool_destroy(struct _workers_pool_t *pool);

static void *_worker_thread(void *v_worker);

static struct _worker_t *_workers_pool_wait(struct _workers_pool_t *pool);
static void _workers_pool_assign(struct _workers_pool_t *pool, struct _worker_t *ready_worker, unsigned buf_index);
static long double _workers_pool_get_fluency_delay(struct _workers_pool_t *pool, struct _worker_t *ready_worker);


struct stream_t *stream_init(struct device_t *dev, struct encoder_t *encoder) {
	struct process_t *proc;
	struct stream_t *stream;

	A_CALLOC(proc, 1);
	atomic_init(&proc->stop, false);
	atomic_init(&proc->slowdown, false);

	A_CALLOC(stream, 1);
	stream->picture = picture_init();
	stream->dev = dev;
	stream->encoder = encoder;
	atomic_init(&stream->updated, false);
	A_MUTEX_INIT(&stream->mutex);
	stream->proc = proc;
	return stream;
}

void stream_destroy(struct stream_t *stream) {
	A_MUTEX_DESTROY(&stream->mutex);
	picture_destroy(stream->picture);
	free(stream->proc);
	free(stream);
}

void stream_loop(struct stream_t *stream) {
	struct _workers_pool_t *pool;

	LOG_INFO("Using V4L2 device: %s", stream->dev->path);
	LOG_INFO("Using desired FPS: %u", stream->dev->desired_fps);

	while ((pool = _stream_init_loop(stream)) != NULL) {
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;
		bool persistent_timeout_reported = false;

		LOG_INFO("Capturing ...");

		LOG_DEBUG("Pre-allocating memory for stream picture ...");
		picture_realloc_data(stream->picture, picture_get_generous_size(stream->dev->run->width, stream->dev->run->height));

		while (!atomic_load(&stream->proc->stop)) {
			struct _worker_t *ready_worker;

			SEP_DEBUG('-');
			LOG_DEBUG("Waiting for worker ...");

			ready_worker = _workers_pool_wait(pool);

			if (!ready_worker->job_failed) {
				if (ready_worker->job_timely) {
					_stream_expose_picture(stream, ready_worker->buf_index, captured_fps);
					LOG_PERF("##### Encoded picture exposed; worker=%u", ready_worker->number);
				} else {
					LOG_PERF("----- Encoded picture dropped; worker=%u", ready_worker->number);
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

			} else if (selected == 0) {
#				ifdef WITH_GPIO
				gpio_set_stream_online(false);
#				endif

				if (stream->dev->persistent) {
					if (!persistent_timeout_reported) {
						LOG_ERROR("Mainloop select() timeout, polling ...")
						persistent_timeout_reported = true;
					}

					continue;
				} else {
					LOG_ERROR("Mainloop select() timeout");
					break;
				}

			} else {
				persistent_timeout_reported = false;

				if (has_read) {
					LOG_DEBUG("Frame is ready");

#					ifdef WITH_GPIO
					gpio_set_stream_online(true);
#					endif

					int buf_index;
					long double now = get_now_monotonic();
					long long now_second = floor_ms(now);

					if ((buf_index = device_grab_buffer(stream->dev)) < 0) {
						break;
					}

					// Workaround for broken, corrupted frames:
					// Under low light conditions corrupted frames may get captured.
					// The good thing is such frames are quite small compared to the regular pictures.
					// For example a VGA (640x480) webcam picture is normally >= 8kByte large,
					// corrupted frames are smaller.
					if (stream->dev->run->hw_buffers[buf_index].used < stream->dev->min_frame_size) {
						LOG_DEBUG("Dropped too small frame sized %zu bytes, assuming it was broken",
							stream->dev->run->hw_buffers[buf_index].used);
						goto pass_frame;
					}

					{
						if (now < grab_after) {
							fluency_passed += 1;
							LOG_VERBOSE("Passed %u frames for fluency: now=%.03Lf, grab_after=%.03Lf", fluency_passed, now, grab_after);
							goto pass_frame;
						}
						fluency_passed = 0;

						if (now_second != captured_fps_second) {
							captured_fps = captured_fps_accum;
							captured_fps_accum = 0;
							captured_fps_second = now_second;
							LOG_PERF_FPS("A new second has come; captured_fps=%u", captured_fps);
						}
						captured_fps_accum += 1;

						long double fluency_delay = _workers_pool_get_fluency_delay(pool, ready_worker);

						grab_after = now + fluency_delay;
						LOG_VERBOSE("Fluency: delay=%.03Lf, grab_after=%.03Lf", fluency_delay, grab_after);
					}

					_workers_pool_assign(pool, ready_worker, buf_index);

					goto next_handlers; // Поток сам освободит буфер

					pass_frame:

					if (device_release_buffer(stream->dev, buf_index) < 0) {
						break;
					}
				}

				next_handlers:

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

		A_MUTEX_LOCK(&stream->mutex);
		stream->online = false;
		atomic_store(&stream->updated, true);
		A_MUTEX_UNLOCK(&stream->mutex);

		_workers_pool_destroy(pool);
		device_switch_capturing(stream->dev, false);
		device_close(stream->dev);

#		ifdef WITH_GPIO
		gpio_set_stream_online(false);
#		endif
	}
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
			sleep(stream->dev->error_delay);
			continue;
		} else {
			SEP_INFO('=');
			access_error = 0;
		}

		if ((pool = _stream_init_one(stream)) == NULL) {
			LOG_INFO("Sleeping %u seconds before new stream init ...", stream->dev->error_delay);
			sleep(stream->dev->error_delay);
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

static void _stream_expose_picture(struct stream_t *stream, unsigned buf_index, unsigned captured_fps) {
	A_MUTEX_LOCK(&stream->mutex);

	picture_copy(stream->dev->run->pictures[buf_index], stream->picture);

	stream->online = true;
	stream->captured_fps = captured_fps;
	atomic_store(&stream->updated, true);

	A_MUTEX_UNLOCK(&stream->mutex);
}

static struct _workers_pool_t *_workers_pool_init(struct stream_t *stream) {
	struct _workers_pool_t *pool;

#	define DEV(_next) stream->dev->_next
#	define RUN(_next) stream->dev->run->_next

	LOG_INFO("Creating pool with %u workers ...", RUN(n_workers));

	A_CALLOC(pool, 1);

	pool->n_workers = RUN(n_workers);
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
#		define WORKER(_next) pool->workers[number]._next

		A_MUTEX_INIT(&WORKER(has_job_mutex));
		atomic_init(&WORKER(has_job), false);
		A_COND_INIT(&WORKER(has_job_cond));

		WORKER(number) = number;
		WORKER(proc_stop) = &stream->proc->stop;
		WORKER(workers_stop) = &pool->workers_stop;

		WORKER(free_workers_mutex) = &pool->free_workers_mutex;
		WORKER(free_workers) = &pool->free_workers;
		WORKER(free_workers_cond) = &pool->free_workers_cond;

		WORKER(dev) = stream->dev;
		WORKER(encoder) = stream->encoder;

		A_THREAD_CREATE(&WORKER(tid), _worker_thread, (void *)&(pool->workers[number]));

		pool->free_workers += 1;

#		undef WORKER
	}
	return pool;
}

static void _workers_pool_destroy(struct _workers_pool_t *pool) {
	LOG_INFO("Destroying workers pool ...");

	atomic_store(&pool->workers_stop, true);
	for (unsigned number = 0; number < pool->n_workers; ++number) {
#		define WORKER(_next) pool->workers[number]._next

		A_MUTEX_LOCK(&WORKER(has_job_mutex));
		atomic_store(&WORKER(has_job), true); // Final job: die
		A_MUTEX_UNLOCK(&WORKER(has_job_mutex));
		A_COND_SIGNAL(&WORKER(has_job_cond));

		A_THREAD_JOIN(WORKER(tid));
		A_MUTEX_DESTROY(&WORKER(has_job_mutex));
		A_COND_DESTROY(&WORKER(has_job_cond));

#		undef WORKER
	}

	A_MUTEX_DESTROY(&pool->free_workers_mutex);
	A_COND_DESTROY(&pool->free_workers_cond);

	free(pool->workers);
	free(pool);
}

static void *_worker_thread(void *v_worker) {
	struct _worker_t *worker = (struct _worker_t *)v_worker;

	A_THREAD_RENAME("worker-%u", worker->number);
	LOG_DEBUG("Hello! I am a worker #%u ^_^", worker->number);

	while (!atomic_load(worker->proc_stop) && !atomic_load(worker->workers_stop)) {
		LOG_DEBUG("Worker %u waiting for a new job ...", worker->number);

		A_MUTEX_LOCK(&worker->has_job_mutex);
		A_COND_WAIT_TRUE(atomic_load(&worker->has_job), &worker->has_job_cond, &worker->has_job_mutex);
		A_MUTEX_UNLOCK(&worker->has_job_mutex);

		if (!atomic_load(worker->workers_stop)) {
#			define PICTURE(_next) worker->dev->run->pictures[worker->buf_index]->_next

			LOG_DEBUG("Worker %u compressing JPEG from buffer %u ...", worker->number, worker->buf_index);

			worker->job_failed = (bool)encoder_compress_buffer(worker->encoder, worker->dev, worker->number, worker->buf_index);

			if (device_release_buffer(worker->dev, worker->buf_index) == 0) {
				if (!worker->job_failed) {
					worker->job_start_ts = PICTURE(encode_begin_ts);
					worker->last_comp_time = PICTURE(encode_end_ts) - worker->job_start_ts;

					LOG_VERBOSE("Compressed new JPEG: size=%zu, time=%0.3Lf, worker=%u, buffer=%u",
						PICTURE(used), worker->last_comp_time, worker->number, worker->buf_index);
				} else {
					LOG_VERBOSE("Compression failed: worker=%u, buffer=%u", worker->number, worker->buf_index);
				}
			} else {
				worker->job_failed = true;
			}

			atomic_store(&worker->has_job, false);

#			undef PICTURE
		}

		A_MUTEX_LOCK(worker->free_workers_mutex);
		*worker->free_workers += 1;
		A_MUTEX_UNLOCK(worker->free_workers_mutex);
		A_COND_SIGNAL(worker->free_workers_cond);
	}

	LOG_DEBUG("Bye-bye (worker %u)", worker->number);
	return NULL;
}

static struct _worker_t *_workers_pool_wait(struct _workers_pool_t *pool) {
	struct _worker_t *ready_worker = NULL;

	A_MUTEX_LOCK(&pool->free_workers_mutex);
	A_COND_WAIT_TRUE(pool->free_workers, &pool->free_workers_cond, &pool->free_workers_mutex);
	A_MUTEX_UNLOCK(&pool->free_workers_mutex);

	if (pool->oldest_worker && !atomic_load(&pool->oldest_worker->has_job)) {
		ready_worker = pool->oldest_worker;
		ready_worker->job_timely = true;
		pool->oldest_worker = pool->oldest_worker->order_next;
	} else {
		for (unsigned number = 0; number < pool->n_workers; ++number) {
			if (
				!atomic_load(&pool->workers[number].has_job) && (
					ready_worker == NULL
					|| ready_worker->job_start_ts < pool->workers[number].job_start_ts
				)
			) {
				ready_worker = &pool->workers[number];
				break;
			}
		}
		assert(ready_worker != NULL);
		ready_worker->job_timely = false; // Освободился воркер, получивший задание позже (или самый первый при самом первом захвате)
	}
	return ready_worker;
}

static void _workers_pool_assign(struct _workers_pool_t *pool, struct _worker_t *ready_worker, unsigned buf_index) {
	if (pool->oldest_worker == NULL) {
		pool->oldest_worker = ready_worker;
		pool->latest_worker = pool->oldest_worker;
	} else {
		if (ready_worker->order_next) {
			ready_worker->order_next->order_prev = ready_worker->order_prev;
		}
		if (ready_worker->order_prev) {
			ready_worker->order_prev->order_next = ready_worker->order_next;
		}
		ready_worker->order_prev = pool->latest_worker;
		pool->latest_worker->order_next = ready_worker;
		pool->latest_worker = ready_worker;
	}
	pool->latest_worker->order_next = NULL;

	A_MUTEX_LOCK(&ready_worker->has_job_mutex);
	ready_worker->buf_index = buf_index;
	atomic_store(&ready_worker->has_job, true);
	A_MUTEX_UNLOCK(&ready_worker->has_job_mutex);
	A_COND_SIGNAL(&ready_worker->has_job_cond);

	A_MUTEX_LOCK(&pool->free_workers_mutex);
	pool->free_workers -= 1;
	A_MUTEX_UNLOCK(&pool->free_workers_mutex);

	LOG_DEBUG("Assigned new frame in buffer %u to worker %u", buf_index, ready_worker->number);
}

static long double _workers_pool_get_fluency_delay(struct _workers_pool_t *pool, struct _worker_t *ready_worker) {
	long double approx_comp_time;
	long double min_delay;

	approx_comp_time = pool->approx_comp_time * 0.9 + ready_worker->last_comp_time * 0.1;

	LOG_VERBOSE("Correcting approx_comp_time: %.3Lf -> %.3Lf (last_comp_time=%.3Lf)",
		pool->approx_comp_time, approx_comp_time, ready_worker->last_comp_time);

	pool->approx_comp_time = approx_comp_time;

	min_delay = pool->approx_comp_time / pool->n_workers; // Среднее время работы размазывается на N воркеров

	if (pool->desired_frames_interval > 0 && min_delay > 0 && pool->desired_frames_interval > min_delay) {
		// Искусственное время задержки на основе желаемого FPS, если включен --desired-fps
		// и аппаратный fps не попадает точно в желаемое значение
		return pool->desired_frames_interval;
	}
	return min_delay;
}
