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

#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "../libs/types.h"
#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/process.h"
#include "../libs/logging.h"
#include "../libs/ring.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/device.h"

#include "blank.h"
#include "encoder.h"
#include "workers.h"
#include "h264.h"
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


typedef struct {
	pthread_t		tid;
	us_device_s		*dev;
	us_queue_s		*queue;
	pthread_mutex_t	*mutex;
	atomic_bool		*stop;
} _releaser_context_s;

typedef struct {
	pthread_t	tid;
	us_queue_s	*queue;
	us_stream_s	*stream;
	atomic_bool	*stop;
} _worker_context_s;


static void _stream_set_capture_state(us_stream_s *stream, uint width, uint height, bool online, uint captured_fps);

static void *_releaser_thread(void *v_ctx);
static void *_jpeg_thread(void *v_ctx);
static void *_h264_thread(void *v_ctx);
static void *_raw_thread(void *v_ctx);

static us_hw_buffer_s *_get_latest_hw(us_queue_s *queue);

static bool _stream_has_jpeg_clients_cached(us_stream_s *stream);
static bool _stream_has_any_clients_cached(us_stream_s *stream);
static int _stream_init_loop(us_stream_s *stream);
static void _stream_expose_jpeg(us_stream_s *stream, const us_frame_s *frame);
static void _stream_expose_raw(us_stream_s *stream, const us_frame_s *frame);
static void _stream_check_suicide(us_stream_s *stream);


us_stream_s *us_stream_init(us_device_s *dev, us_encoder_s *enc) {
	us_stream_runtime_s *run;
	US_CALLOC(run, 1);
	US_RING_INIT_WITH_ITEMS(run->http_jpeg_ring, 4, us_frame_init);
	atomic_init(&run->http_has_clients, false);
	atomic_init(&run->http_snapshot_requested, 0);
	atomic_init(&run->http_last_request_ts, 0);
	atomic_init(&run->http_capture_state, 0);
	atomic_init(&run->stop, false);
	run->blank = us_blank_init();

	us_stream_s *stream;
	US_CALLOC(stream, 1);
	stream->dev = dev;
	stream->enc = enc;
	stream->error_delay = 1;
	stream->h264_bitrate = 5000; // Kbps
	stream->h264_gop = 30;
	stream->run = run;

	us_blank_draw(run->blank, "< NO SIGNAL >", dev->width, dev->height);
	_stream_set_capture_state(stream, dev->width, dev->height, false, 0);
	return stream;
}

void us_stream_destroy(us_stream_s *stream) {
	us_blank_destroy(stream->run->blank);
	US_RING_DELETE_WITH_ITEMS(stream->run->http_jpeg_ring, us_frame_destroy);
	free(stream->run);
	free(stream);
}

void us_stream_loop(us_stream_s *stream) {
	us_stream_runtime_s *const run = stream->run;
	us_device_s *const dev = stream->dev;

	US_LOG_INFO("Using V4L2 device: %s", dev->path);
	US_LOG_INFO("Using desired FPS: %u", dev->desired_fps);

	atomic_store(&run->http_last_request_ts, us_get_now_monotonic());

	if (stream->h264_sink != NULL) {
		run->h264 = us_h264_stream_init(stream->h264_sink, stream->h264_m2m_path, stream->h264_bitrate, stream->h264_gop);
	}

	while (!_stream_init_loop(stream)) {
		atomic_bool threads_stop;
		atomic_init(&threads_stop, false);

		pthread_mutex_t release_mutex;
		US_MUTEX_INIT(release_mutex);
		const uint n_releasers = dev->run->n_bufs;
		_releaser_context_s *releasers;
		US_CALLOC(releasers, n_releasers);
		for (uint index = 0; index < n_releasers; ++index) {
			_releaser_context_s *ctx = &releasers[index];
			ctx->dev = dev;
			ctx->queue = us_queue_init(1);
			ctx->mutex = &release_mutex;
			ctx->stop = &threads_stop;
			US_THREAD_CREATE(ctx->tid, _releaser_thread, ctx);
		}

		_worker_context_s jpeg_ctx = {
			.queue = us_queue_init(dev->run->n_bufs),
			.stream = stream,
			.stop = &threads_stop,
		};
		US_THREAD_CREATE(jpeg_ctx.tid, _jpeg_thread, &jpeg_ctx);

		_worker_context_s h264_ctx;
		if (run->h264 != NULL) {
			h264_ctx.queue = us_queue_init(dev->run->n_bufs);
			h264_ctx.stream = stream;
			h264_ctx.stop = &threads_stop;
			US_THREAD_CREATE(h264_ctx.tid, _h264_thread, &h264_ctx);
		}

		_worker_context_s raw_ctx;
		if (stream->raw_sink != NULL) {
			raw_ctx.queue = us_queue_init(2);
			raw_ctx.stream = stream;
			raw_ctx.stop = &threads_stop;
			US_THREAD_CREATE(raw_ctx.tid, _raw_thread, &raw_ctx);
		}

		uint captured_fps_accum = 0;
		sll captured_fps_ts = 0;
		uint captured_fps = 0;

		US_LOG_INFO("Capturing ...");

		uint slowdown_count = 0;
		while (!atomic_load(&run->stop) && !atomic_load(&threads_stop)) {
			us_hw_buffer_s *hw;
			const int buf_index = us_device_grab_buffer(dev, &hw);
			switch (buf_index) {
				case -2: continue; // Broken frame
				case -1: goto close; // Error
			}
			assert(buf_index >= 0);

			const sll now_sec_ts = us_floor_ms(us_get_now_monotonic());
			if (now_sec_ts != captured_fps_ts) {
				captured_fps = captured_fps_accum;
				captured_fps_accum = 0;
				captured_fps_ts = now_sec_ts;
				US_LOG_PERF_FPS("A new second has come; captured_fps=%u", captured_fps);
			}
			captured_fps_accum += 1;

			_stream_set_capture_state(stream, dev->run->width, dev->run->height, true, captured_fps);
#			ifdef WITH_GPIO
			us_gpio_set_stream_online(true);
#			endif

			us_device_buffer_incref(hw); // JPEG
			us_queue_put(jpeg_ctx.queue, hw, 0);
			if (run->h264 != NULL) {
				us_device_buffer_incref(hw); // H264
				us_queue_put(h264_ctx.queue, hw, 0);
			}
			if (stream->raw_sink != NULL) {
				us_device_buffer_incref(hw); // RAW
				us_queue_put(raw_ctx.queue, hw, 0);
			}
			us_queue_put(releasers[buf_index].queue, hw, 0); // Plan to release

			// Мы не обновляем здесь состояние синков, потому что это происходит внутри обслуживающих их потоков
			_stream_check_suicide(stream);
			if (stream->slowdown && !_stream_has_any_clients_cached(stream)) {
				usleep(100 * 1000);
				slowdown_count = (slowdown_count + 1) % 10;
				if (slowdown_count > 0) {
					continue;
				}
			}
		}

	close:
		atomic_store(&threads_stop, true);

		if (stream->raw_sink != NULL) {
			US_THREAD_JOIN(raw_ctx.tid);
			us_queue_destroy(raw_ctx.queue);
		}

		if (run->h264 != NULL) {
			US_THREAD_JOIN(h264_ctx.tid);
			us_queue_destroy(h264_ctx.queue);
		}

		US_THREAD_JOIN(jpeg_ctx.tid);
		us_queue_destroy(jpeg_ctx.queue);

		for (uint index = 0; index < n_releasers; ++index) {
			US_THREAD_JOIN(releasers[index].tid);
			us_queue_destroy(releasers[index].queue);
		}
		free(releasers);
		US_MUTEX_DESTROY(release_mutex);

		atomic_store(&threads_stop, false);

		us_encoder_close(stream->enc);
		us_device_close(dev);

		if (!atomic_load(&run->stop)) {
			US_SEP_INFO('=');
		}
	}

	US_DELETE(run->h264, us_h264_stream_destroy);
}

void us_stream_loop_break(us_stream_s *stream) {
	atomic_store(&stream->run->stop, true);
}

void us_stream_get_capture_state(us_stream_s *stream, uint *width, uint *height, bool *online, uint *captured_fps) {
	const u64 state = atomic_load(&stream->run->http_capture_state);
	*width = state & 0xFFFF;
	*height = (state >> 16) & 0xFFFF;
	*captured_fps = (state >> 32) & 0xFFFF;
	*online = (state >> 48) & 1;
}

void _stream_set_capture_state(us_stream_s *stream, uint width, uint height, bool online, uint captured_fps) {
	const u64 state = (
		(u64)(width & 0xFFFF)
		| ((u64)(height & 0xFFFF) << 16)
		| ((u64)(captured_fps & 0xFFFF) << 32)
		| ((u64)(online ? 1 : 0) << 48)
	);
	atomic_store(&stream->run->http_capture_state, state);
}

static void *_releaser_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_rel")
	_releaser_context_s *ctx = v_ctx;

	while (!atomic_load(ctx->stop)) {
		us_hw_buffer_s *hw;
		if (us_queue_get(ctx->queue, (void**)&hw, 0.1) < 0) {
			continue;
		}

		while (atomic_load(&hw->refs) > 0) {
			if (atomic_load(ctx->stop)) {
				goto done;
			}
			usleep(5 * 1000);
		}

		US_MUTEX_LOCK(*ctx->mutex);
		const int released = us_device_release_buffer(ctx->dev, hw);
		US_MUTEX_UNLOCK(*ctx->mutex);
		if (released < 0) {
			goto done;
		}
	}

done:
	atomic_store(ctx->stop, true); // Stop all other guys on error
	return NULL;
}

static void *_jpeg_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_jpeg")
	_worker_context_s *ctx = v_ctx;
	us_stream_s *stream = ctx->stream;

	ldf grab_after = 0;
	uint fluency_passed = 0;

	while (!atomic_load(ctx->stop)) {
		us_worker_s *const ready_wr = us_workers_pool_wait(stream->enc->run->pool);
		us_encoder_job_s *const ready_job = ready_wr->job;

		if (ready_job->hw != NULL) {
			us_device_buffer_decref(ready_job->hw);
			ready_job->hw = NULL;
			if (ready_wr->job_failed) {
				// pass
			} else if (ready_wr->job_timely) {
				_stream_expose_jpeg(stream, ready_job->dest);
				if (atomic_load(&stream->run->http_snapshot_requested) > 0) { // Process real snapshots
					atomic_fetch_sub(&stream->run->http_snapshot_requested, 1);
				}
				US_LOG_PERF("##### Encoded JPEG exposed; worker=%s, latency=%.3Lf",
					ready_wr->name, us_get_now_monotonic() - ready_job->dest->grab_ts);
			} else {
				US_LOG_PERF("----- Encoded JPEG dropped; worker=%s", ready_wr->name);
			}
		}

		us_hw_buffer_s *hw = _get_latest_hw(ctx->queue);
		if (hw == NULL) {
			continue;
		}

		const bool update_required = (stream->jpeg_sink != NULL && us_memsink_server_check(stream->jpeg_sink, NULL));
		if (!update_required && !_stream_has_jpeg_clients_cached(stream)) {
			US_LOG_VERBOSE("Passed JPEG encoding because nobody is watching");
			us_device_buffer_decref(hw);
			continue;
		}

		const ldf now_ts = us_get_now_monotonic();
		if (now_ts < grab_after) {
			fluency_passed += 1;
			US_LOG_VERBOSE("Passed %u JPEG frames for fluency: now=%.03Lf, grab_after=%.03Lf",
				fluency_passed, now_ts, grab_after);
			us_device_buffer_decref(hw);
			continue;
		}
		fluency_passed = 0;

		const ldf fluency_delay = us_workers_pool_get_fluency_delay(stream->enc->run->pool, ready_wr);
		grab_after = now_ts + fluency_delay;
		US_LOG_VERBOSE("Fluency: delay=%.03Lf, grab_after=%.03Lf", fluency_delay, grab_after);

		ready_job->hw = hw;
		us_workers_pool_assign(stream->enc->run->pool, ready_wr);
		US_LOG_DEBUG("Assigned new frame in buffer=%d to worker=%s", hw->buf.index, ready_wr->name);
	}
	return NULL;
}

static void *_h264_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_h264");
	_worker_context_s *ctx = v_ctx;

	ldf last_encode_ts = us_get_now_monotonic();
	while (!atomic_load(ctx->stop)) {
		us_hw_buffer_s *hw = _get_latest_hw(ctx->queue);
		if (hw == NULL) {
			continue;
		}

		if (!us_memsink_server_check(ctx->stream->run->h264->sink, NULL)) {
			us_device_buffer_decref(hw);
			US_LOG_VERBOSE("Passed H264 encoding because nobody is watching");
			continue;
		}

		// Форсим кейфрейм, если от захвата давно не было фреймов
		const ldf now_ts = us_get_now_monotonic();
		const bool force_key = (last_encode_ts + 0.5 < now_ts);
		last_encode_ts = now_ts;

		us_h264_stream_process(ctx->stream->run->h264, &hw->raw, force_key);
		us_device_buffer_decref(hw);
	}
	return NULL;
}

static void *_raw_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_raw");
	_worker_context_s *ctx = v_ctx;

	while (!atomic_load(ctx->stop)) {
		us_hw_buffer_s *hw = _get_latest_hw(ctx->queue);
		if (hw == NULL) {
			continue;
		}

		if (!us_memsink_server_check(ctx->stream->raw_sink, NULL)) {
			us_device_buffer_decref(hw);
			US_LOG_VERBOSE("Passed RAW publishing because nobody is watching");
			continue;
		}

		us_memsink_server_put(ctx->stream->raw_sink, &hw->raw, false);
		us_device_buffer_decref(hw);
	}
	return NULL;
}

static us_hw_buffer_s *_get_latest_hw(us_queue_s *queue) {
	us_hw_buffer_s *hw;
	if (us_queue_get(queue, (void**)&hw, 0.1) < 0) {
		return NULL;
	}
	while (!us_queue_is_empty(queue)) { // Берем только самый свежий кадр
		us_device_buffer_decref(hw);
		assert(!us_queue_get(queue, (void**)&hw, 0));
	}
	return hw;
}

static bool _stream_has_jpeg_clients_cached(us_stream_s *stream) {
	const us_stream_runtime_s *const run = stream->run;
	return (
		atomic_load(&run->http_has_clients)
		|| (atomic_load(&run->http_snapshot_requested) > 0)
		|| (stream->jpeg_sink != NULL && atomic_load(&stream->jpeg_sink->has_clients))
	);
}

static bool _stream_has_any_clients_cached(us_stream_s *stream) {
	const us_stream_runtime_s *const run = stream->run;
	return (
		_stream_has_jpeg_clients_cached(stream)
		|| (run->h264 != NULL && atomic_load(&run->h264->sink->has_clients))
		|| (stream->raw_sink != NULL && atomic_load(&stream->raw_sink->has_clients))
	);
}

static int _stream_init_loop(us_stream_s *stream) {
	us_stream_runtime_s *const run = stream->run;

	bool waiting_reported = false;
	while (!atomic_load(&stream->run->stop)) {
		// Флаги has_clients у синков не обновляются сами по себе, поэтому обновим их
		// на каждой итерации старта стрима. После старта этим будут заниматься воркеры.
		if (stream->jpeg_sink != NULL) {
			us_memsink_server_check(stream->jpeg_sink, NULL);
		}
		if (stream->run->h264 != NULL) {
			us_memsink_server_check(stream->run->h264->sink, NULL);
		}
		if (stream->raw_sink != NULL) {
			us_memsink_server_check(stream->raw_sink, NULL);
		}

		_stream_check_suicide(stream);

		uint width = stream->dev->run->width;
		uint height = stream->dev->run->height;
		if (width == 0 || height == 0) {
			width = stream->dev->width;
			height = stream->dev->height;
		}
		us_blank_draw(run->blank, "< NO SIGNAL >", width, height);

		_stream_set_capture_state(stream, width, height, false, 0);
#		ifdef WITH_GPIO
		us_gpio_set_stream_online(false);
#		endif

		_stream_expose_jpeg(stream, run->blank->jpeg);
		if (run->h264 != NULL) {
			us_h264_stream_process(run->h264, run->blank->raw, true);
		}
		_stream_expose_raw(stream, run->blank->raw);

		stream->dev->dma_export = (
			stream->enc->type == US_ENCODER_TYPE_M2M_VIDEO
			|| stream->enc->type == US_ENCODER_TYPE_M2M_IMAGE
			|| run->h264 != NULL
		);
		switch (us_device_open(stream->dev)) {
			case -2:
				if (!waiting_reported) {
					waiting_reported = true;
					US_LOG_INFO("Waiting for the capture device ...");
				}
				goto sleep_and_retry;
			case -1:
				waiting_reported = false;
				goto sleep_and_retry;
			default: break;
		}
		us_encoder_open(stream->enc, stream->dev);
		return 0;

	sleep_and_retry:
		for (uint count = 0; count < stream->error_delay * 10; ++count) {
			if (atomic_load(&run->stop)) {
				break;
			}
			usleep(100 * 1000);
		}
	}
	return -1;
}

static void _stream_expose_jpeg(us_stream_s *stream, const us_frame_s *frame) {
	us_stream_runtime_s *const run = stream->run;
	int ri;
	while ((ri = us_ring_producer_acquire(run->http_jpeg_ring, 0)) < 0) {
		if (atomic_load(&run->stop)) {
			return;
		}
	}
	us_frame_s *const dest = run->http_jpeg_ring->items[ri];
	us_frame_copy(frame, dest);
	us_ring_producer_release(run->http_jpeg_ring, ri);
	if (stream->jpeg_sink != NULL) {
		us_memsink_server_put(stream->jpeg_sink, dest, NULL);
	}
}

static void _stream_expose_raw(us_stream_s *stream, const us_frame_s *frame) {
	if (stream->raw_sink != NULL) {
		us_memsink_server_put(stream->raw_sink, frame, NULL);
	}
}

static void _stream_check_suicide(us_stream_s *stream) {
	if (stream->exit_on_no_clients == 0) {
		return;
	}
	us_stream_runtime_s *const run = stream->run;
	const ldf now_ts = us_get_now_monotonic();
	const ull http_last_request_ts = atomic_load(&run->http_last_request_ts); // Seconds
	if (_stream_has_any_clients_cached(stream)) {
		atomic_store(&run->http_last_request_ts, now_ts);
	} else if (http_last_request_ts + stream->exit_on_no_clients < now_ts) {
		US_LOG_INFO("No requests or HTTP/sink clients found in last %u seconds, exiting ...",
			stream->exit_on_no_clients);
		us_process_suicide();
		atomic_store(&run->http_last_request_ts, now_ts);
	}
}
