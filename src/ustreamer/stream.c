/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "../libs/types.h"
#include "../libs/errors.h"
#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/process.h"
#include "../libs/logging.h"
#include "../libs/ring.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/capture.h"
#include "../libs/unjpeg.h"
#include "../libs/fpsi.h"
#ifdef WITH_V4P
#	include "../libs/drm/drm.h"
#endif

#include "blank.h"
#include "encoder.h"
#include "workers.h"
#include "m2m.h"
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


typedef struct {
	pthread_t		tid;
	us_capture_s	*cap;
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


static void *_releaser_thread(void *v_ctx);
static void *_jpeg_thread(void *v_ctx);
static void *_raw_thread(void *v_ctx);
static void *_h264_thread(void *v_ctx);
#ifdef WITH_V4P
static void *_drm_thread(void *v_ctx);
#endif

static us_capture_hwbuf_s *_get_latest_hw(us_queue_s *queue);

static bool _stream_has_jpeg_clients_cached(us_stream_s *stream);
static bool _stream_has_any_clients_cached(us_stream_s *stream);
static int _stream_init_loop(us_stream_s *stream);
static void _stream_update_captured_fpsi(us_stream_s *stream, const us_frame_s *frame, bool bump);
#ifdef WITH_V4P
static void _stream_drm_ensure_no_signal(us_stream_s *stream);
#endif
static void _stream_expose_jpeg(us_stream_s *stream, const us_frame_s *frame);
static void _stream_expose_raw(us_stream_s *stream, const us_frame_s *frame);
static void _stream_encode_expose_h264(us_stream_s *stream, const us_frame_s *frame, bool force_key);
static void _stream_check_suicide(us_stream_s *stream);


us_stream_s *us_stream_init(us_capture_s *cap, us_encoder_s *enc) {
	us_stream_http_s *http;
	US_CALLOC(http, 1);
#	ifdef WITH_V4P
	http->drm_fpsi = us_fpsi_init("DRM", true);
#	endif
	http->h264_fpsi = us_fpsi_init("H264", true);
	US_RING_INIT_WITH_ITEMS(http->jpeg_ring, 4, us_frame_init);
	atomic_init(&http->has_clients, false);
	atomic_init(&http->snapshot_requested, 0);
	atomic_init(&http->last_request_ts, 0);
	http->captured_fpsi = us_fpsi_init("STREAM-CAPTURED", true);

	us_stream_runtime_s *run;
	US_CALLOC(run, 1);
	atomic_init(&run->stop, false);
	run->blank = us_blank_init();
	run->http = http;

	us_stream_s *stream;
	US_CALLOC(stream, 1);
	stream->cap = cap;
	stream->enc = enc;
	stream->error_delay = 1;
	stream->h264_bitrate = 5000; // Kbps
	stream->h264_gop = 30;
	stream->run = run;

	us_stream_update_blank(stream, cap); // Init blank
	return stream;
}

void us_stream_update_blank(us_stream_s *stream, const us_capture_s *cap) {
	us_stream_runtime_s *const run = stream->run;
	us_blank_draw(run->blank, "< NO SIGNAL >", cap->width, cap->height);
	us_fpsi_frame_to_meta(run->blank->raw, &run->notify_meta); // Initial "unchanged" meta
	_stream_update_captured_fpsi(stream, run->blank->raw, false);
}

void us_stream_destroy(us_stream_s *stream) {
	us_fpsi_destroy(stream->run->http->captured_fpsi);
	US_RING_DELETE_WITH_ITEMS(stream->run->http->jpeg_ring, us_frame_destroy);
	us_fpsi_destroy(stream->run->http->h264_fpsi);
#	ifdef WITH_V4P
	us_fpsi_destroy(stream->run->http->drm_fpsi);
#	endif
	us_blank_destroy(stream->run->blank);
	free(stream->run->http);
	free(stream->run);
	free(stream);
}

void us_stream_loop(us_stream_s *stream) {
	us_stream_runtime_s *const run = stream->run;
	us_capture_s *const cap = stream->cap;

	atomic_store(&run->http->last_request_ts, us_get_now_monotonic());

	if (stream->h264_sink != NULL) {
		run->h264_enc = us_m2m_h264_encoder_init("H264", stream->h264_m2m_path, stream->h264_bitrate, stream->h264_gop);
		run->h264_tmp_src = us_frame_init();
		run->h264_dest = us_frame_init();
	}

	while (!_stream_init_loop(stream)) {
		atomic_bool threads_stop;
		atomic_init(&threads_stop, false);

		pthread_mutex_t release_mutex;
		US_MUTEX_INIT(release_mutex);
		const uint n_releasers = cap->run->n_bufs;
		_releaser_context_s *releasers;
		US_CALLOC(releasers, n_releasers);
		for (uint index = 0; index < n_releasers; ++index) {
			_releaser_context_s *ctx = &releasers[index];
			ctx->cap = cap;
			ctx->queue = us_queue_init(1);
			ctx->mutex = &release_mutex;
			ctx->stop = &threads_stop;
			US_THREAD_CREATE(ctx->tid, _releaser_thread, ctx);
		}

#		define CREATE_WORKER(x_cond, x_ctx, x_thread, x_capacity) \
			_worker_context_s *x_ctx = NULL; \
			if (x_cond) { \
				US_CALLOC(x_ctx, 1); \
				x_ctx->queue = us_queue_init(x_capacity); \
				x_ctx->stream = stream; \
				x_ctx->stop = &threads_stop; \
				US_THREAD_CREATE(x_ctx->tid, (x_thread), x_ctx); \
			}
		CREATE_WORKER(true, jpeg_ctx, _jpeg_thread, cap->run->n_bufs);
		CREATE_WORKER((stream->raw_sink != NULL), raw_ctx, _raw_thread, 2);
		CREATE_WORKER((stream->h264_sink != NULL), h264_ctx, _h264_thread, cap->run->n_bufs);
#		ifdef WITH_V4P
		CREATE_WORKER((stream->drm != NULL), drm_ctx, _drm_thread, cap->run->n_bufs); // cppcheck-suppress assertWithSideEffect
#		endif
#		undef CREATE_WORKER

		US_LOG_INFO("Capturing ...");

		uint slowdown_count = 0;
		while (!atomic_load(&run->stop) && !atomic_load(&threads_stop)) {
			us_capture_hwbuf_s *hw;
			switch (us_capture_hwbuf_grab(cap, &hw)) {
				case 0 ... INT_MAX: break; // Grabbed buffer number
				case US_ERROR_NO_DATA: continue; // Broken frame
				default: goto close; // Any error
			}

			_stream_update_captured_fpsi(stream, &hw->raw, true);

#			ifdef WITH_GPIO
			us_gpio_set_stream_online(true);
#			endif

#			define QUEUE_HW(x_ctx) if (x_ctx != NULL) { \
					us_capture_hwbuf_incref(hw); \
					us_queue_put(x_ctx->queue, hw, 0); \
				}
			QUEUE_HW(jpeg_ctx);
			QUEUE_HW(raw_ctx);
			QUEUE_HW(h264_ctx);
#			ifdef WITH_V4P
			QUEUE_HW(drm_ctx);
#			endif
#			undef QUEUE_HW
			us_queue_put(releasers[hw->buf.index].queue, hw, 0); // Plan to release

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

#		define DELETE_WORKER(x_ctx) if (x_ctx != NULL) { \
				US_THREAD_JOIN(x_ctx->tid); \
				us_queue_destroy(x_ctx->queue); \
				free(x_ctx); \
			}
#		ifdef WITH_V4P
		DELETE_WORKER(drm_ctx);
#		endif
		DELETE_WORKER(h264_ctx);
		DELETE_WORKER(raw_ctx);
		DELETE_WORKER(jpeg_ctx);
#		undef DELETE_WORKER

		for (uint index = 0; index < n_releasers; ++index) {
			US_THREAD_JOIN(releasers[index].tid);
			us_queue_destroy(releasers[index].queue);
		}
		free(releasers);
		US_MUTEX_DESTROY(release_mutex);

		atomic_store(&threads_stop, false);

		us_encoder_close(stream->enc);
		us_capture_close(cap);

		if (!atomic_load(&run->stop)) {
			US_SEP_INFO('=');
		}
	}

	US_DELETE(run->h264_enc, us_m2m_encoder_destroy);
	US_DELETE(run->h264_tmp_src, us_frame_destroy);
	US_DELETE(run->h264_dest, us_frame_destroy);
}

void us_stream_loop_break(us_stream_s *stream) {
	atomic_store(&stream->run->stop, true);
}

static void *_releaser_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_rel")
	_releaser_context_s *ctx = v_ctx;

	while (!atomic_load(ctx->stop)) {
		us_capture_hwbuf_s *hw;
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
		const int released = us_capture_hwbuf_release(ctx->cap, hw);
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

	ldf grab_after_ts = 0;
	uint fluency_passed = 0;

	while (!atomic_load(ctx->stop)) {
		us_worker_s *const wr = us_workers_pool_wait(stream->enc->run->pool);
		us_encoder_job_s *const job = wr->job;

		if (job->hw != NULL) {
			us_capture_hwbuf_decref(job->hw);
			job->hw = NULL;
			if (wr->job_failed) {
				// pass
			} else if (wr->job_timely) {
				_stream_expose_jpeg(stream, job->dest);
				if (atomic_load(&stream->run->http->snapshot_requested) > 0) { // Process real snapshots
					atomic_fetch_sub(&stream->run->http->snapshot_requested, 1);
				}
				US_LOG_PERF("JPEG: ##### Encoded JPEG exposed; worker=%s, latency=%.3Lf",
					wr->name, us_get_now_monotonic() - job->dest->grab_ts);
			} else {
				US_LOG_PERF("JPEG: ----- Encoded JPEG dropped; worker=%s", wr->name);
			}
		}

		us_capture_hwbuf_s *hw = _get_latest_hw(ctx->queue);
		if (hw == NULL) {
			continue;
		}

		const bool update_required = (stream->jpeg_sink != NULL && us_memsink_server_check(stream->jpeg_sink, NULL));
		if (!update_required && !_stream_has_jpeg_clients_cached(stream)) {
			US_LOG_VERBOSE("JPEG: Passed encoding because nobody is watching");
			us_capture_hwbuf_decref(hw);
			continue;
		}

		const ldf now_ts = us_get_now_monotonic();
		if (now_ts < grab_after_ts) {
			fluency_passed += 1;
			US_LOG_VERBOSE("JPEG: Passed %u frames for fluency: now=%.03Lf, grab_after=%.03Lf",
				fluency_passed, now_ts, grab_after_ts);
			us_capture_hwbuf_decref(hw);
			continue;
		}
		fluency_passed = 0;

		const ldf fluency_delay = us_workers_pool_get_fluency_delay(stream->enc->run->pool, wr);
		grab_after_ts = now_ts + fluency_delay;
		US_LOG_VERBOSE("JPEG: Fluency: delay=%.03Lf, grab_after=%.03Lf", fluency_delay, grab_after_ts);

		job->hw = hw;
		us_workers_pool_assign(stream->enc->run->pool, wr);
		US_LOG_DEBUG("JPEG: Assigned new frame in buffer=%d to worker=%s", hw->buf.index, wr->name);
	}
	return NULL;
}

static void *_raw_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_raw");
	_worker_context_s *ctx = v_ctx;

	while (!atomic_load(ctx->stop)) {
		us_capture_hwbuf_s *hw = _get_latest_hw(ctx->queue);
		if (hw == NULL) {
			continue;
		}

		if (us_memsink_server_check(ctx->stream->raw_sink, NULL)) {
			us_memsink_server_put(ctx->stream->raw_sink, &hw->raw, false);
		} else {
			US_LOG_VERBOSE("RAW: Passed publishing because nobody is watching");
		}
		us_capture_hwbuf_decref(hw);
	}
	return NULL;
}

static void *_h264_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_h264");
	_worker_context_s *ctx = v_ctx;
	us_stream_s *stream = ctx->stream;

	ldf grab_after_ts = 0;
	while (!atomic_load(ctx->stop)) {
		us_capture_hwbuf_s *hw = _get_latest_hw(ctx->queue);
		if (hw == NULL) {
			continue;
		}

		if (!us_memsink_server_check(stream->h264_sink, NULL)) {
			US_LOG_VERBOSE("H264: Passed encoding because nobody is watching");
			goto decref;
		}
		if (hw->raw.grab_ts < grab_after_ts) {
			US_LOG_DEBUG("H264: Passed encoding for FPS limit");
			goto decref;
		}

		_stream_encode_expose_h264(ctx->stream, &hw->raw, false);

		// M2M-енкодер увеличивает задержку на 100 милисекунд при 1080p, если скормить ему больше 30 FPS.
		// Поэтому у нас есть два режима: 60 FPS для маленьких видео и 30 для 1920x1080(1200).
		// Следующй фрейм захватывается не раньше, чем это требуется по FPS, минус небольшая
		// погрешность (если захват неравномерный) - немного меньше 1/60, и примерно треть от 1/30.
		const uint fps_limit = stream->run->h264_enc->run->fps_limit;
		if (fps_limit > 0) {
			const ldf frame_interval = (ldf)1 / fps_limit;
			grab_after_ts = hw->raw.grab_ts + frame_interval - 0.01;
		}

	decref:
		us_capture_hwbuf_decref(hw);
	}
	return NULL;
}

#ifdef WITH_V4P
static void *_drm_thread(void *v_ctx) {
	US_THREAD_SETTLE("str_drm");
	_worker_context_s *ctx = v_ctx;
	us_stream_s *stream = ctx->stream;

	// Close previously opened DRM for a stub
	us_drm_close(stream->drm);

	us_capture_hwbuf_s *prev_hw = NULL;
	while (!atomic_load(ctx->stop)) {
#		define CHECK(x_arg) if ((x_arg) < 0) { goto close; }
#		define SLOWDOWN { \
				const ldf m_next_ts = us_get_now_monotonic() + 1; \
				while (!atomic_load(ctx->stop) && us_get_now_monotonic() < m_next_ts) { \
					us_capture_hwbuf_s *m_pass_hw = _get_latest_hw(ctx->queue); \
					if (m_pass_hw != NULL) { \
						us_capture_hwbuf_decref(m_pass_hw); \
					} \
				} \
			}

		CHECK(us_drm_open(stream->drm, ctx->stream->cap));

		while (!atomic_load(ctx->stop)) {
			CHECK(us_drm_wait_for_vsync(stream->drm));
			US_DELETE(prev_hw, us_capture_hwbuf_decref);

			us_capture_hwbuf_s *hw = _get_latest_hw(ctx->queue);
			if (hw == NULL) {
				continue;
			}

			if (stream->drm->run->opened == 0) {
				CHECK(us_drm_expose_dma(stream->drm, hw));
				prev_hw = hw;
				us_fpsi_meta_s meta = {.online = true}; // Online means live video
				us_fpsi_update(stream->run->http->drm_fpsi, true, &meta);
				continue;
			}

			CHECK(us_drm_expose_stub(stream->drm, stream->drm->run->opened, ctx->stream->cap));
			us_capture_hwbuf_decref(hw);

			us_fpsi_meta_s meta = {.online = false};
			us_fpsi_update(stream->run->http->drm_fpsi, true, &meta);

			SLOWDOWN;
		}

	close:
		us_drm_close(stream->drm);
		US_DELETE(prev_hw, us_capture_hwbuf_decref);
		us_fpsi_meta_s meta = {.online = false};
		us_fpsi_update(stream->run->http->drm_fpsi, false, &meta);
		SLOWDOWN;

#		undef SLOWDOWN
#		undef CHECK
	}
	return NULL;
}
#endif

static us_capture_hwbuf_s *_get_latest_hw(us_queue_s *queue) {
	us_capture_hwbuf_s *hw;
	if (us_queue_get(queue, (void**)&hw, 0.1) < 0) {
		return NULL;
	}
	while (!us_queue_is_empty(queue)) { // Берем только самый свежий кадр
		us_capture_hwbuf_decref(hw);
		assert(!us_queue_get(queue, (void**)&hw, 0));
	}
	return hw;
}

static bool _stream_has_jpeg_clients_cached(us_stream_s *stream) {
	const us_stream_runtime_s *const run = stream->run;
	return (
		atomic_load(&run->http->has_clients)
		|| (atomic_load(&run->http->snapshot_requested) > 0)
		|| (stream->jpeg_sink != NULL && atomic_load(&stream->jpeg_sink->has_clients))
	);
}

static bool _stream_has_any_clients_cached(us_stream_s *stream) {
	return (
		_stream_has_jpeg_clients_cached(stream)
		|| (stream->h264_sink != NULL && atomic_load(&stream->h264_sink->has_clients))
		|| (stream->raw_sink != NULL && atomic_load(&stream->raw_sink->has_clients))
#		ifdef WITH_V4P
		|| (stream->drm != NULL)
#		endif
	);
}

static int _stream_init_loop(us_stream_s *stream) {
	us_stream_runtime_s *const run = stream->run;

	int once = 0;
	while (!atomic_load(&stream->run->stop)) {
#		ifdef WITH_GPIO
		us_gpio_set_stream_online(false);
#		endif

		// Флаги has_clients у синков не обновляются сами по себе, поэтому обновим их
		// на каждой итерации старта стрима. После старта этим будут заниматься воркеры.
#		define UPDATE_SINK(x_sink) if (x_sink != NULL) { us_memsink_server_check(x_sink, NULL); }
		UPDATE_SINK(stream->jpeg_sink);
		UPDATE_SINK(stream->raw_sink);
		UPDATE_SINK(stream->h264_sink);
#		undef UPDATE_SINK

		_stream_check_suicide(stream);

		stream->cap->dma_export = (
			stream->enc->type == US_ENCODER_TYPE_M2M_VIDEO
			|| stream->enc->type == US_ENCODER_TYPE_M2M_IMAGE
			|| stream->h264_sink != NULL
#			ifdef WITH_V4P
			|| stream->drm != NULL
#			endif
		);
		switch (us_capture_open(stream->cap)) {
			case 0: break;
			case US_ERROR_NO_DEVICE:
			case US_ERROR_NO_DATA:
				US_ONCE({ US_LOG_INFO("Waiting for the capture device ..."); });
				goto offline_and_retry;
			default:
				once = 0;
				goto offline_and_retry;
		}
		us_encoder_open(stream->enc, stream->cap);
		return 0;

	offline_and_retry:
		for (uint count = 0; count < stream->error_delay * 10; ++count) {
			if (atomic_load(&run->stop)) {
				break;
			}
			if (count % 10 == 0) {
				// Каждую секунду повторяем blank
				uint width = stream->cap->run->width;
				uint height = stream->cap->run->height;
				if (width == 0 || height == 0) {
					width = stream->cap->width;
					height = stream->cap->height;
				}
				us_blank_draw(run->blank, "< NO SIGNAL >", width, height);

				_stream_update_captured_fpsi(stream, run->blank->raw, false);
				_stream_expose_jpeg(stream, run->blank->jpeg);
				_stream_expose_raw(stream, run->blank->raw);
				_stream_encode_expose_h264(stream, run->blank->raw, true);

#				ifdef WITH_V4P
				_stream_drm_ensure_no_signal(stream);
#				endif
			}
			usleep(100 * 1000);
		}
	}
	return -1;
}

static void _stream_update_captured_fpsi(us_stream_s *stream, const us_frame_s *frame, bool bump) {
	us_stream_runtime_s *const run = stream->run;

	us_fpsi_meta_s meta = {0};
	us_fpsi_frame_to_meta(frame, &meta);
	us_fpsi_update(run->http->captured_fpsi, bump, &meta);

	if (stream->notify_parent && !memcmp(&run->notify_meta, &meta, sizeof(us_fpsi_meta_s))) {
		memcpy(&run->notify_meta, &meta, sizeof(us_fpsi_meta_s));
		us_process_notify_parent();
	}
}

#ifdef WITH_V4P
static void _stream_drm_ensure_no_signal(us_stream_s *stream) {
	if (stream->drm == NULL) {
		return;
	}

	const us_fpsi_meta_s meta = {.online = false};
	if (stream->drm->run->opened <= 0) {
		us_drm_close(stream->drm);
		if (us_drm_open(stream->drm, NULL) < 0) {
			goto close;
		}
	}
	if (us_drm_ensure_no_signal(stream->drm) < 0) {
		goto close;
	}
	us_fpsi_update(stream->run->http->drm_fpsi, true, &meta);
	return;

close:
	us_fpsi_update(stream->run->http->drm_fpsi, false, &meta);
	us_drm_close(stream->drm);
}
#endif

static void _stream_expose_jpeg(us_stream_s *stream, const us_frame_s *frame) {
	us_stream_runtime_s *const run = stream->run;
	int ri;
	while ((ri = us_ring_producer_acquire(run->http->jpeg_ring, 0)) < 0) {
		if (atomic_load(&run->stop)) {
			return;
		}
	}
	us_frame_s *const dest = run->http->jpeg_ring->items[ri];
	us_frame_copy(frame, dest);
	us_ring_producer_release(run->http->jpeg_ring, ri);
	if (stream->jpeg_sink != NULL) {
		us_memsink_server_put(stream->jpeg_sink, dest, NULL);
	}
}

static void _stream_expose_raw(us_stream_s *stream, const us_frame_s *frame) {
	if (stream->raw_sink != NULL) {
		us_memsink_server_put(stream->raw_sink, frame, NULL);
	}
}

static void _stream_encode_expose_h264(us_stream_s *stream, const us_frame_s *frame, bool force_key) {
	if (stream->h264_sink == NULL) {
		return;
	}
	us_stream_runtime_s *run = stream->run;

	us_fpsi_meta_s meta = {.online = false};
	if (us_is_jpeg(frame->format)) {
		if (us_unjpeg(frame, run->h264_tmp_src, true) < 0) {
			goto done;
		}
		frame = run->h264_tmp_src;
	}
	if (run->h264_key_requested) {
		US_LOG_INFO("H264: Requested keyframe by a sink client");
		run->h264_key_requested = false;
		force_key = true;
	}
	if (!us_m2m_encoder_compress(run->h264_enc, frame, run->h264_dest, force_key)) {
		meta.online = !us_memsink_server_put(stream->h264_sink, run->h264_dest, &run->h264_key_requested);
	}

done:
	us_fpsi_update(run->http->h264_fpsi, meta.online, &meta);
}

static void _stream_check_suicide(us_stream_s *stream) {
	if (stream->exit_on_no_clients == 0) {
		return;
	}
	us_stream_runtime_s *const run = stream->run;

	const ldf now_ts = us_get_now_monotonic();
	const ull http_last_request_ts = atomic_load(&run->http->last_request_ts); // Seconds
	if (_stream_has_any_clients_cached(stream)) {
		atomic_store(&run->http->last_request_ts, now_ts);
	} else if (http_last_request_ts + stream->exit_on_no_clients < now_ts) {
		US_LOG_INFO("No requests or HTTP/sink clients found in last %u seconds, exiting ...",
			stream->exit_on_no_clients);
		us_process_suicide();
		atomic_store(&run->http->last_request_ts, now_ts);
	}
}
