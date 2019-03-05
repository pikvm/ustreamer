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


#include <errno.h>
#include <time.h>
#include <assert.h>

#include <pthread.h>

#include <sys/select.h>
#include <linux/videodev2.h>

#include "tools.h"
#include "logging.h"
#include "xioctl.h"
#include "device.h"
#include "encoder.h"
#include "stream.h"


static long double _stream_get_fluency_delay(struct device_t *dev, struct workers_pool_t *pool);
static void _stream_expose_picture(struct stream_t *stream, unsigned buf_index);

static int _stream_init_loop(struct device_t *dev, struct workers_pool_t *pool);
static int _stream_init(struct device_t *dev, struct workers_pool_t *pool);

static void _stream_init_workers(struct device_t *dev, struct workers_pool_t *pool);
static void *_stream_worker_thread(void *v_ctx);
static void _stream_destroy_workers(struct device_t *dev, struct workers_pool_t *pool);

static int _stream_control(struct device_t *dev, bool enable);
static int _stream_grab_buffer(struct device_t *dev, struct v4l2_buffer *buf_info);
static int _stream_release_buffer(struct device_t *dev, struct v4l2_buffer *buf_info);
static int _stream_handle_event(struct device_t *dev);


struct stream_t *stream_init(struct device_t *dev, struct encoder_t *encoder) {
	struct stream_t *stream;

	A_CALLOC(stream, 1);
	stream->dev = dev;
	stream->encoder = encoder;
	A_PTHREAD_M_INIT(&stream->mutex);
	return stream;
}

void stream_destroy(struct stream_t *stream) {
	A_PTHREAD_M_DESTROY(&stream->mutex);
	free(stream);
}

void stream_loop(struct stream_t *stream) {
	struct workers_pool_t pool;
	bool workers_stop;

	MEMSET_ZERO(pool);
	pool.encoder = stream->encoder;
	pool.workers_stop = &workers_stop;

	LOG_INFO("Using V4L2 device: %s", stream->dev->path);
	LOG_INFO("Using desired FPS: %u", stream->dev->desired_fps);

	while (_stream_init_loop(stream->dev, &pool) == 0) {
		struct worker_t *oldest_worker = NULL;
		struct worker_t *last_worker = NULL;
		long double grab_after = 0;
		unsigned fluency_passed = 0;
		unsigned captured_fps_accum = 0;
		long long captured_fps_second = 0;
		bool persistent_timeout_reported = false;

		LOG_DEBUG("Allocation memory for stream picture ...");
		A_CALLOC(stream->picture.data, stream->dev->run->max_picture_size);

		LOG_INFO("Capturing ...");

		while (!stream->dev->stop) {
			int free_worker_number = -1;

			SEP_DEBUG('-');

			LOG_DEBUG("Waiting for workers ...");
			A_PTHREAD_M_LOCK(&pool.free_workers_mutex);
			A_PTHREAD_C_WAIT_TRUE(pool.free_workers, &pool.free_workers_cond, &pool.free_workers_mutex);
			A_PTHREAD_M_UNLOCK(&pool.free_workers_mutex);

			if (oldest_worker && !oldest_worker->has_job && oldest_worker->ctx.buf_index >= 0) {
				if (oldest_worker->job_failed) {
					break;
				}

				_stream_expose_picture(stream, oldest_worker->ctx.buf_index);

				free_worker_number = oldest_worker->ctx.number;
				oldest_worker = oldest_worker->order_next;

				LOG_PERF("##### Raw frame accepted; worker = %u", free_worker_number);
			} else {
				for (unsigned number = 0; number < stream->dev->n_workers; ++number) {
					if (!pool.workers[number].has_job && (free_worker_number == -1
						|| pool.workers[free_worker_number].job_start_time < pool.workers[number].job_start_time
					)) {
						free_worker_number = number;
						break;
					}
				}

				assert(free_worker_number >= 0);
				assert(!pool.workers[free_worker_number].has_job);

				LOG_PERF("----- Raw frame dropped;  worker = %u", free_worker_number);
			}

			if (stream->dev->stop) {
				break;
			}

#	define INIT_FD_SET(_set) \
		fd_set _set; FD_ZERO(&_set); FD_SET(stream->dev->run->fd, &_set);

			INIT_FD_SET(read_fds);
			INIT_FD_SET(write_fds);
			INIT_FD_SET(error_fds);

#	undef INIT_FD_SET

			struct timeval timeout;
			timeout.tv_sec = stream->dev->timeout;
			timeout.tv_usec = 0;

			LOG_DEBUG("Calling select() on video device ...");
			int retval = select(stream->dev->run->fd + 1, &read_fds, &write_fds, &error_fds, &timeout);
			LOG_DEBUG("Device select() --> %d", retval);

			if (retval < 0) {
				if (errno != EINTR) {
					LOG_PERROR("Mainloop select() error");
					break;
				}

			} else if (retval == 0) {
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

				if (FD_ISSET(stream->dev->run->fd, &read_fds)) {
					LOG_DEBUG("Frame is ready");

					struct v4l2_buffer buf_info;
					long double now = get_now_monotonic();
					long long now_second = floor_ms(now);

					if (_stream_grab_buffer(stream->dev, &buf_info) < 0) {
						break;
					}
					stream->dev->run->pictures[buf_info.index].grab_time = now;

					// Workaround for broken, corrupted frames:
					// Under low light conditions corrupted frames may get captured.
					// The good thing is such frames are quite small compared to the regular pictures.
					// For example a VGA (640x480) webcam picture is normally >= 8kByte large,
					// corrupted frames are smaller.
					if (buf_info.bytesused < stream->dev->min_frame_size) {
						LOG_DEBUG("Dropping too small frame sized %u bytes, assuming it as broken", buf_info.bytesused);
						goto pass_frame;
					}

					{
						if (now < grab_after) {
							fluency_passed += 1;
							LOG_VERBOSE("Passed %u frames for fluency: now=%.03Lf; grab_after=%.03Lf", fluency_passed, now, grab_after);
							goto pass_frame;
						}
						fluency_passed = 0;

						if (now_second != captured_fps_second) {
							stream->captured_fps = captured_fps_accum;
							captured_fps_accum = 0;
							captured_fps_second = now_second;
							LOG_PERF("Oldest worker complete, Captured-FPS = %u", stream->captured_fps);
						}
						captured_fps_accum += 1;

						long double fluency_delay = _stream_get_fluency_delay(stream->dev, &pool);

						grab_after = now + fluency_delay;
						LOG_VERBOSE("Fluency: delay=%.03Lf; grab_after=%.03Lf", fluency_delay, grab_after);
					}

					LOG_DEBUG("Grabbed a new frame to buffer %u", buf_info.index);
					pool.workers[free_worker_number].ctx.buf_info = buf_info;

					if (!oldest_worker) {
						oldest_worker = &pool.workers[free_worker_number];
						last_worker = oldest_worker;
					} else {
						if (pool.workers[free_worker_number].order_next) {
							pool.workers[free_worker_number].order_next->order_prev = pool.workers[free_worker_number].order_prev;
						}
						if (pool.workers[free_worker_number].order_prev) {
							pool.workers[free_worker_number].order_prev->order_next = pool.workers[free_worker_number].order_next;
						}
						pool.workers[free_worker_number].order_prev = last_worker;
						last_worker->order_next = &pool.workers[free_worker_number];
						last_worker = &pool.workers[free_worker_number];
					}
					last_worker->order_next = NULL;

					A_PTHREAD_M_LOCK(&pool.workers[free_worker_number].has_job_mutex);
					pool.workers[free_worker_number].ctx.buf_index = buf_info.index;
					pool.workers[free_worker_number].has_job = true;
					A_PTHREAD_M_UNLOCK(&pool.workers[free_worker_number].has_job_mutex);
					A_PTHREAD_C_SIGNAL(&pool.workers[free_worker_number].has_job_cond);

					A_PTHREAD_M_LOCK(&pool.free_workers_mutex);
					pool.free_workers -= 1;
					A_PTHREAD_M_UNLOCK(&pool.free_workers_mutex);

					goto next_handlers; // Поток сам освободит буфер

					pass_frame:

					if (_stream_release_buffer(stream->dev, &buf_info) < 0) {
						break;
					}
				}

				next_handlers:

				if (FD_ISSET(stream->dev->run->fd, &write_fds)) {
					LOG_ERROR("Got unexpected writing event, seems device was disconnected");
					break;
				}

				if (FD_ISSET(stream->dev->run->fd, &error_fds)) {
					LOG_INFO("Got V4L2 event");
					if (_stream_handle_event(stream->dev) < 0) {
						break;
					}
				}
			}
		}

		A_PTHREAD_M_LOCK(&stream->mutex);
		stream->picture.size = 0; // On stream offline
		free(stream->picture.data);
		stream->width = 0;
		stream->height = 0;
		stream->updated = true;
		A_PTHREAD_M_UNLOCK(&stream->mutex);
	}

	_stream_destroy_workers(stream->dev, &pool);
	_stream_control(stream->dev, false);
	device_close(stream->dev);
}

void stream_loop_break(struct stream_t *stream) {
	stream->dev->stop = 1;
}

static void _stream_expose_picture(struct stream_t *stream, unsigned buf_index) {
#	define PICTURE(_next) stream->dev->run->pictures[buf_index]._next

	A_PTHREAD_M_LOCK(&stream->mutex);

	stream->picture.size = PICTURE(size);
	stream->picture.allocated = PICTURE(allocated);
	memcpy(
		stream->picture.data, PICTURE(data),
		stream->picture.size * sizeof(*stream->picture.data)
	);
	stream->picture.grab_time = PICTURE(grab_time);
	stream->picture.encode_begin_time = PICTURE(encode_begin_time);
	stream->picture.encode_end_time = PICTURE(encode_end_time);

	stream->width = stream->dev->run->width;
	stream->height = stream->dev->run->height;
	stream->updated = true;

	A_PTHREAD_M_UNLOCK(&stream->mutex);

#	undef PICTURE
}

static long double _stream_get_fluency_delay(struct device_t *dev, struct workers_pool_t *pool) {
	long double sum_comp_time = 0;
	long double avg_comp_time;
	long double min_delay;
	long double soft_delay;

	for (unsigned number = 0; number < dev->n_workers; ++number) {
		A_PTHREAD_M_LOCK(&pool->workers[number].last_comp_time_mutex);
		if (pool->workers[number].last_comp_time > 0) {
			sum_comp_time += pool->workers[number].last_comp_time;
		}
		A_PTHREAD_M_UNLOCK(&pool->workers[number].last_comp_time_mutex);
	}
	avg_comp_time = sum_comp_time / dev->n_workers; // Среднее время работы воркеров

	min_delay = avg_comp_time / dev->n_workers; // Среднее время работы размазывается на N воркеров

	if (dev->desired_fps > 0 && min_delay > 0) {
		// Искусственное время задержки на основе желаемого FPS, если включен --desired-fps
		soft_delay = ((long double) 1) / dev->desired_fps - sum_comp_time;
		return (min_delay > soft_delay ? min_delay : soft_delay);
	}

	return min_delay;
}

static int _stream_init_loop(struct device_t *dev, struct workers_pool_t *pool) {
	int retval = -1;

	LOG_DEBUG("%s: *dev->stop = %d", __FUNCTION__, dev->stop);
	while (!dev->stop) {
		if ((retval = _stream_init(dev, pool)) < 0) {
			LOG_INFO("Sleeping %u seconds before new stream init ...", dev->error_delay);
			sleep(dev->error_delay);
		} else {
			break;
		}
	}
	return retval;
}

static int _stream_init(struct device_t *dev, struct workers_pool_t *pool) {
	SEP_INFO('=');

	_stream_destroy_workers(dev, pool);
	_stream_control(dev, false);
	device_close(dev);

	if (device_open(dev) < 0) {
		goto error;
	}
	if (_stream_control(dev, true) < 0) {
		goto error;
	}

	encoder_prepare_live(pool->encoder, dev);

	_stream_init_workers(dev, pool);

	return 0;

	error:
		device_close(dev);
		return -1;
}

static void _stream_init_workers(struct device_t *dev, struct workers_pool_t *pool) {
	LOG_INFO("Spawning %u workers ...", dev->n_workers);

	*pool->workers_stop = false;
	A_CALLOC(pool->workers, dev->n_workers);

	A_PTHREAD_M_INIT(&pool->free_workers_mutex);
	A_PTHREAD_C_INIT(&pool->free_workers_cond);

	for (unsigned number = 0; number < dev->n_workers; ++number) {
		pool->free_workers += 1;

		A_PTHREAD_M_INIT(&pool->workers[number].has_job_mutex);
		A_PTHREAD_C_INIT(&pool->workers[number].has_job_cond);

#	define CTX(_next) pool->workers[number].ctx._next

		CTX(number)			= number;
		CTX(dev)			= dev;
		CTX(dev_stop)		= (sig_atomic_t *volatile)&dev->stop;
		CTX(workers_stop)	= pool->workers_stop;

		CTX(encoder) = pool->encoder;

		CTX(last_comp_time_mutex)	= &pool->workers[number].last_comp_time_mutex;
		CTX(last_comp_time)			= &pool->workers[number].last_comp_time;

		CTX(has_job_mutex)	= &pool->workers[number].has_job_mutex;
		CTX(has_job)		= &pool->workers[number].has_job;
		CTX(job_failed)		= &pool->workers[number].job_failed;
		CTX(job_start_time)	= &pool->workers[number].job_start_time;
		CTX(has_job_cond)	= &pool->workers[number].has_job_cond;

		CTX(free_workers_mutex)	= &pool->free_workers_mutex;
		CTX(free_workers)		= &pool->free_workers;
		CTX(free_workers_cond)	= &pool->free_workers_cond;

#	undef CTX

		A_PTHREAD_CREATE(&pool->workers[number].tid, _stream_worker_thread, (void *)&pool->workers[number].ctx);
	}
}

static void *_stream_worker_thread(void *v_ctx) {
	struct worker_context_t *ctx = (struct worker_context_t *)v_ctx;

	LOG_DEBUG("Hello! I am a worker #%u ^_^", ctx->number);

	while (!*ctx->dev_stop && !*ctx->workers_stop) {
		LOG_DEBUG("Worker %u waiting for a new job ...", ctx->number);
		A_PTHREAD_M_LOCK(ctx->has_job_mutex);
		A_PTHREAD_C_WAIT_TRUE(*ctx->has_job, ctx->has_job_cond, ctx->has_job_mutex);
		A_PTHREAD_M_UNLOCK(ctx->has_job_mutex);

#	define PICTURE(_next) ctx->dev->run->pictures[ctx->buf_index]._next

		if (!*ctx->workers_stop) {
			LOG_DEBUG("Worker %u compressing JPEG from buffer %u ...", ctx->number, ctx->buf_index);

			PICTURE(encode_begin_time) = get_now_monotonic();
			if (encoder_compress_buffer(ctx->encoder, ctx->dev, ctx->number, ctx->buf_index) < 0) {
				*ctx->job_failed = true;
			}
			PICTURE(encode_end_time) = get_now_monotonic();

			if (_stream_release_buffer(ctx->dev, &ctx->buf_info) == 0) {
				*ctx->job_start_time = PICTURE(encode_begin_time);
				*ctx->has_job = false;

				long double last_comp_time = PICTURE(encode_end_time) - *ctx->job_start_time;

				A_PTHREAD_M_LOCK(ctx->last_comp_time_mutex);
				*ctx->last_comp_time = last_comp_time;
				A_PTHREAD_M_UNLOCK(ctx->last_comp_time_mutex);

				LOG_VERBOSE(
					"Compressed JPEG size=%zu; time=%0.3Lf; worker=%u; buffer=%u",
					PICTURE(size), last_comp_time, ctx->number, ctx->buf_index
				);
			} else {
				*ctx->job_failed = true;
				*ctx->has_job = false;
			}
		}

#	undef PICTURE

		A_PTHREAD_M_LOCK(ctx->free_workers_mutex);
		*ctx->free_workers += 1;
		A_PTHREAD_M_UNLOCK(ctx->free_workers_mutex);
		A_PTHREAD_C_SIGNAL(ctx->free_workers_cond);
	}

	LOG_DEBUG("Bye-bye (worker %u)", ctx->number);
	return NULL;
}

static void _stream_destroy_workers(struct device_t *dev, struct workers_pool_t *pool) {
	if (pool->workers) {
		LOG_INFO("Destroying workers ...");

		*pool->workers_stop = true;
		for (unsigned number = 0; number < dev->n_workers; ++number) {
			A_PTHREAD_M_LOCK(&pool->workers[number].has_job_mutex);
			pool->workers[number].has_job = true; // Final job: die
			A_PTHREAD_M_UNLOCK(&pool->workers[number].has_job_mutex);
			A_PTHREAD_C_SIGNAL(&pool->workers[number].has_job_cond);

			A_PTHREAD_JOIN(pool->workers[number].tid);
			A_PTHREAD_M_DESTROY(&pool->workers[number].has_job_mutex);
			A_PTHREAD_C_DESTROY(&pool->workers[number].has_job_cond);
		}

		A_PTHREAD_M_DESTROY(&pool->free_workers_mutex);
		A_PTHREAD_C_DESTROY(&pool->free_workers_cond);

		free(pool->workers);
	}
	pool->free_workers = 0;
	pool->workers = NULL;
}

static int _stream_control(struct device_t *dev, bool enable) {
	if (enable != dev->run->capturing) {
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		LOG_DEBUG("Calling ioctl(%s) ...", (enable ? "VIDIOC_STREAMON" : "VIDIOC_STREAMOFF"));
		if (xioctl(dev->run->fd, (enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF), &type) < 0) {
			LOG_PERROR("Unable to %s capturing", (enable ? "start" : "stop"));
			if (enable) {
				return -1;
			}
		}

		dev->run->capturing = enable;
		LOG_INFO("Capturing %s", (enable ? "started" : "stopped"));
	}
    return 0;
}

static int _stream_grab_buffer(struct device_t *dev, struct v4l2_buffer *buf_info) {
	MEMSET_ZERO_PTR(buf_info);
	buf_info->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf_info->memory = V4L2_MEMORY_MMAP;

	LOG_DEBUG("Calling ioctl(VIDIOC_DQBUF) ...");
	if (xioctl(dev->run->fd, VIDIOC_DQBUF, buf_info) < 0) {
		LOG_PERROR("Unable to dequeue buffer");
		return -1;
	}

	LOG_DEBUG("Got a new frame in buffer index=%u; bytesused=%u", buf_info->index, buf_info->bytesused);
	if (buf_info->index >= dev->run->n_buffers) {
		LOG_ERROR("Got invalid buffer index=%u; nbuffers=%u", buf_info->index, dev->run->n_buffers);
		return -1;
	}
	return 0;
}

static int _stream_release_buffer(struct device_t *dev, struct v4l2_buffer *buf_info) {
	LOG_DEBUG("Calling ioctl(VIDIOC_QBUF) ...");
	if (xioctl(dev->run->fd, VIDIOC_QBUF, buf_info) < 0) {
		LOG_PERROR("Unable to requeue buffer");
		return -1;
	}
	return 0;
}

static int _stream_handle_event(struct device_t *dev) {
	struct v4l2_event event;

	LOG_DEBUG("Calling ioctl(VIDIOC_DQEVENT) ...");
	if (!xioctl(dev->run->fd, VIDIOC_DQEVENT, &event)) {
		switch (event.type) {
			case V4L2_EVENT_SOURCE_CHANGE:
				LOG_INFO("Got V4L2_EVENT_SOURCE_CHANGE: source changed");
				return -1;
			case V4L2_EVENT_EOS:
				LOG_INFO("Got V4L2_EVENT_EOS: end of stream (ignored)");
				return 0;
		}
	} else {
		LOG_PERROR("Got some V4L2 device event, but where is it? ");
	}
	return 0;
}
