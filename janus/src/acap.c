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


#include "acap.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>

#include <pthread.h>
#include <alsa/asoundlib.h>
#include <speex/speex_resampler.h>
#include <opus/opus.h>

#include "uslibs/types.h"
#include "uslibs/errors.h"
#include "uslibs/tools.h"
#include "uslibs/array.h"
#include "uslibs/ring.h"
#include "uslibs/threading.h"

#include "rtp.h"
#include "logging.h"


#define _JLOG_PERROR_ALSA(_err, _prefix, _msg, ...)	US_JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, snd_strerror(_err))
#define _JLOG_PERROR_RES(_err, _prefix, _msg, ...)	US_JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, speex_resampler_strerror(_err))
#define _JLOG_PERROR_OPUS(_err, _prefix, _msg, ...)	US_JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, opus_strerror(_err))

// A number of frames per 1 channel:
//   - https://github.com/xiph/opus/blob/7b05f44/src/opus_demo.c#L368
// #define _HZ_TO_FRAMES(_hz)	(6 * (_hz) / 50) // 120ms
#define _HZ_TO_FRAMES(_hz)	((_hz) / 50) // 20ms
#define _HZ_TO_BUF16(_hz)	(_HZ_TO_FRAMES(_hz) * US_RTP_OPUS_CH) // ... * 2: One stereo frame = (16bit L) + (16bit R)
#define _HZ_TO_BUF8(_hz)	(_HZ_TO_BUF16(_hz) * sizeof(s16))

#define _MIN_PCM_HZ			8000
#define _MAX_PCM_HZ			192000
#define _MAX_BUF16			_HZ_TO_BUF16(_MAX_PCM_HZ)
#define _MAX_BUF8			_HZ_TO_BUF8(_MAX_PCM_HZ)


typedef struct {
	s16		data[_MAX_BUF16];
} _pcm_buffer_s;

typedef struct {
	u8		data[US_RTP_PAYLOAD_SIZE];
	uz		used;
	u64		pts;
} _enc_buffer_s;


static _pcm_buffer_s *_pcm_buffer_init(void);
static _enc_buffer_s *_enc_buffer_init(void);

static void *_pcm_thread(void *v_acap);
static void *_encoder_thread(void *v_acap);


bool us_acap_probe(const char *name) {
	snd_pcm_t *pcm;
	int err;
	US_JLOG_INFO("acap", "Probing PCM capture ...");
	if ((err = snd_pcm_open(&pcm, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		_JLOG_PERROR_ALSA(err, "acap", "Can't probe PCM capture");
		return false;
	}
	snd_pcm_close(pcm);
	US_JLOG_INFO("acap", "PCM capture is available");
	return true;
}

us_acap_s *us_acap_init(const char *name, uint pcm_hz) {
	us_acap_s *acap;
	US_CALLOC(acap, 1);
	acap->pcm_hz = pcm_hz;
	US_RING_INIT_WITH_ITEMS(acap->pcm_ring, 8, _pcm_buffer_init);
	US_RING_INIT_WITH_ITEMS(acap->enc_ring, 8, _enc_buffer_init);
	atomic_init(&acap->stop, false);

	int err;

	{
		if ((err = snd_pcm_open(&acap->pcm, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			acap->pcm = NULL;
			_JLOG_PERROR_ALSA(err, "acap", "Can't open PCM capture");
			goto error;
		}
		assert(!snd_pcm_hw_params_malloc(&acap->pcm_params));

#		define SET_PARAM(_msg, _func, ...) { \
				if ((err = _func(acap->pcm, acap->pcm_params, ##__VA_ARGS__)) < 0) { \
					_JLOG_PERROR_ALSA(err, "acap", _msg); \
					goto error; \
				} \
			}

		SET_PARAM("Can't initialize PCM params",	snd_pcm_hw_params_any);
		SET_PARAM("Can't set PCM access type",		snd_pcm_hw_params_set_access, SND_PCM_ACCESS_RW_INTERLEAVED);
		SET_PARAM("Can't set PCM channels number",	snd_pcm_hw_params_set_channels, US_RTP_OPUS_CH);
		SET_PARAM("Can't set PCM sampling format",	snd_pcm_hw_params_set_format, SND_PCM_FORMAT_S16_LE);
		SET_PARAM("Can't set PCM sampling rate",	snd_pcm_hw_params_set_rate_near, &acap->pcm_hz, 0);
		if (acap->pcm_hz < _MIN_PCM_HZ || acap->pcm_hz > _MAX_PCM_HZ) {
			US_JLOG_ERROR("acap", "Unsupported PCM freq: %u; should be: %u <= F <= %u",
				acap->pcm_hz, _MIN_PCM_HZ, _MAX_PCM_HZ);
			goto error;
		}
		acap->pcm_frames = _HZ_TO_FRAMES(acap->pcm_hz);
		acap->pcm_size = _HZ_TO_BUF8(acap->pcm_hz);
		SET_PARAM("Can't apply PCM params", snd_pcm_hw_params);

#		undef SET_PARAM
	}

	if (acap->pcm_hz != US_RTP_OPUS_HZ) {
		acap->res = speex_resampler_init(US_RTP_OPUS_CH, acap->pcm_hz, US_RTP_OPUS_HZ, SPEEX_RESAMPLER_QUALITY_DESKTOP, &err);
		if (err < 0) {
			acap->res = NULL;
			_JLOG_PERROR_RES(err, "acap", "Can't create resampler");
			goto error;
		}
	}

	{
		// OPUS_APPLICATION_VOIP, OPUS_APPLICATION_RESTRICTED_LOWDELAY
		acap->enc = opus_encoder_create(US_RTP_OPUS_HZ, US_RTP_OPUS_CH, OPUS_APPLICATION_AUDIO, &err);
		assert(err == 0);
		// https://github.com/meetecho/janus-gateway/blob/3cdd6ff/src/plugins/janus_audiobridge.c#L2272
		// https://datatracker.ietf.org/doc/html/rfc7587#section-3.1.1
		assert(!opus_encoder_ctl(acap->enc, OPUS_SET_BITRATE(128000)));
		assert(!opus_encoder_ctl(acap->enc, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)));
		assert(!opus_encoder_ctl(acap->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)));
		// OPUS_SET_INBAND_FEC(1), OPUS_SET_PACKET_LOSS_PERC(10): see rtpa.c
	}

	US_JLOG_INFO("acap", "Pipeline configured on %uHz; capturing ...", acap->pcm_hz);
	acap->tids_created = true;
	US_THREAD_CREATE(acap->enc_tid, _encoder_thread, acap);
	US_THREAD_CREATE(acap->pcm_tid, _pcm_thread, acap);

	return acap;

	error:
		us_acap_destroy(acap);
		return NULL;
}

void us_acap_destroy(us_acap_s *acap) {
	if (acap->tids_created) {
		atomic_store(&acap->stop, true);
		US_THREAD_JOIN(acap->pcm_tid);
		US_THREAD_JOIN(acap->enc_tid);
	}
	US_DELETE(acap->enc, opus_encoder_destroy);
	US_DELETE(acap->res, speex_resampler_destroy);
	US_DELETE(acap->pcm, snd_pcm_close);
	US_DELETE(acap->pcm_params, snd_pcm_hw_params_free);
	US_RING_DELETE_WITH_ITEMS(acap->enc_ring, free);
	US_RING_DELETE_WITH_ITEMS(acap->pcm_ring, free);
	if (acap->tids_created) {
		US_JLOG_INFO("acap", "Pipeline closed");
	}
	free(acap);
}

int us_acap_get_encoded(us_acap_s *acap, u8 *data, uz *size, u64 *pts) {
	if (atomic_load(&acap->stop)) {
		return -1;
	}
	const int ri = us_ring_consumer_acquire(acap->enc_ring, 0.1);
	if (ri < 0) {
		return US_ERROR_NO_DATA;
	}
	const _enc_buffer_s *const buf = acap->enc_ring->items[ri];
	if (*size < buf->used) {
		us_ring_consumer_release(acap->enc_ring, ri);
		return US_ERROR_NO_DATA;
	}
	memcpy(data, buf->data, buf->used);
	*size = buf->used;
	*pts = buf->pts;
	us_ring_consumer_release(acap->enc_ring, ri);
	return 0;
}

static _pcm_buffer_s *_pcm_buffer_init(void) {
	_pcm_buffer_s *buf;
	US_CALLOC(buf, 1);
	return buf;
}

static _enc_buffer_s *_enc_buffer_init(void) {
	_enc_buffer_s *buf;
	US_CALLOC(buf, 1);
	return buf;
}

static void *_pcm_thread(void *v_acap) {
	US_THREAD_SETTLE("us_ac_pcm");

	us_acap_s *const acap = v_acap;
	u8 in[_MAX_BUF8];

	while (!atomic_load(&acap->stop)) {
		const int frames = snd_pcm_readi(acap->pcm, in, acap->pcm_frames);
		if (frames < 0) {
			_JLOG_PERROR_ALSA(frames, "acap", "Fatal: Can't capture PCM frames");
			break;
		} else if (frames < (int)acap->pcm_frames) {
			US_JLOG_ERROR("acap", "Fatal: Too few PCM frames captured");
			break;
		}

		const int ri = us_ring_producer_acquire(acap->pcm_ring, 0);
		if (ri >= 0) {
			_pcm_buffer_s *const out = acap->pcm_ring->items[ri];
			memcpy(out->data, in, acap->pcm_size);
			us_ring_producer_release(acap->pcm_ring, ri);
		} else {
			US_JLOG_ERROR("acap", "PCM ring is full");
		}
	}

	atomic_store(&acap->stop, true);
	return NULL;
}

static void *_encoder_thread(void *v_acap) {
	US_THREAD_SETTLE("us_a_enc");

	us_acap_s *const acap = v_acap;
	s16 in_res[_MAX_BUF16];

	while (!atomic_load(&acap->stop)) {
		const int in_ri = us_ring_consumer_acquire(acap->pcm_ring, 0.1);
		if (in_ri < 0) {
			continue;
		}
		_pcm_buffer_s *const in = acap->pcm_ring->items[in_ri];

		s16 *in_ptr;
		if (acap->res != NULL) {
			assert(acap->pcm_hz != US_RTP_OPUS_HZ);
			u32 in_count = acap->pcm_frames;
			u32 out_count = _HZ_TO_FRAMES(US_RTP_OPUS_HZ);
			speex_resampler_process_interleaved_int(acap->res, in->data, &in_count, in_res, &out_count);
			in_ptr = in_res;
		} else {
			assert(acap->pcm_hz == US_RTP_OPUS_HZ);
			in_ptr = in->data;
		}

		const int out_ri = us_ring_producer_acquire(acap->enc_ring, 0);
		if (out_ri < 0) {
			US_JLOG_ERROR("acap", "OPUS encoder queue is full");
			us_ring_consumer_release(acap->pcm_ring, in_ri);
			continue;
		}
		_enc_buffer_s *const out = acap->enc_ring->items[out_ri];

		const int size = opus_encode(acap->enc, in_ptr, _HZ_TO_FRAMES(US_RTP_OPUS_HZ), out->data, US_ARRAY_LEN(out->data));
		us_ring_consumer_release(acap->pcm_ring, in_ri);

		if (size >= 0) {
			out->used = size;
			out->pts = acap->pts;
			// https://datatracker.ietf.org/doc/html/rfc7587#section-4.2
			acap->pts += _HZ_TO_FRAMES(US_RTP_OPUS_HZ);
		} else {
			_JLOG_PERROR_OPUS(size, "acap", "Fatal: Can't encode PCM frame to OPUS");
		}
		us_ring_producer_release(acap->enc_ring, out_ri);
	}

	atomic_store(&acap->stop, true);
	return NULL;
}
