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
#include "au.h"
#include "logging.h"


static void *_pcm_thread(void *v_acap);
static void *_encoder_thread(void *v_acap);


bool us_acap_probe(const char *name) {
	snd_pcm_t *dev;
	int err;
	US_JLOG_INFO("acap", "Probing PCM capture ...");
	if ((err = snd_pcm_open(&dev, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		US_JLOG_PERROR_ALSA(err, "acap", "Can't probe PCM capture");
		return false;
	}
	snd_pcm_close(dev);
	US_JLOG_INFO("acap", "PCM capture is available");
	return true;
}

us_acap_s *us_acap_init(const char *name, uint pcm_hz) {
	us_acap_s *acap;
	US_CALLOC(acap, 1);
	acap->pcm_hz = pcm_hz;
	US_RING_INIT_WITH_ITEMS(acap->pcm_ring, 8, us_au_pcm_init);
	US_RING_INIT_WITH_ITEMS(acap->enc_ring, 8, us_au_encoded_init);
	atomic_init(&acap->stop, false);

	int err;

	{
		if ((err = snd_pcm_open(&acap->dev, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			acap->dev = NULL;
			US_JLOG_PERROR_ALSA(err, "acap", "Can't open PCM capture");
			goto error;
		}
		assert(!snd_pcm_hw_params_malloc(&acap->dev_params));

#		define SET_PARAM(_msg, _func, ...) { \
				if ((err = _func(acap->dev, acap->dev_params, ##__VA_ARGS__)) < 0) { \
					US_JLOG_PERROR_ALSA(err, "acap", _msg); \
					goto error; \
				} \
			}

		SET_PARAM("Can't initialize PCM params",	snd_pcm_hw_params_any);
		SET_PARAM("Can't set PCM access type",		snd_pcm_hw_params_set_access, SND_PCM_ACCESS_RW_INTERLEAVED);
		SET_PARAM("Can't set PCM channels number",	snd_pcm_hw_params_set_channels, US_RTP_OPUS_CH);
		SET_PARAM("Can't set PCM sampling format",	snd_pcm_hw_params_set_format, SND_PCM_FORMAT_S16_LE);
		SET_PARAM("Can't set PCM sampling rate",	snd_pcm_hw_params_set_rate_near, &acap->pcm_hz, 0);
		if (acap->pcm_hz < US_AU_MIN_PCM_HZ || acap->pcm_hz > US_AU_MAX_PCM_HZ) {
			US_JLOG_ERROR("acap", "Unsupported PCM freq: %u; should be: %u <= F <= %u",
				acap->pcm_hz, US_AU_MIN_PCM_HZ, US_AU_MAX_PCM_HZ);
			goto error;
		}
		acap->pcm_frames = US_AU_HZ_TO_FRAMES(acap->pcm_hz);
		acap->pcm_size = US_AU_HZ_TO_BUF8(acap->pcm_hz);
		SET_PARAM("Can't apply PCM params", snd_pcm_hw_params);

#		undef SET_PARAM
	}

	if (acap->pcm_hz != US_RTP_OPUS_HZ) {
		acap->res = speex_resampler_init(US_RTP_OPUS_CH, acap->pcm_hz, US_RTP_OPUS_HZ, SPEEX_RESAMPLER_QUALITY_DESKTOP, &err);
		if (err < 0) {
			acap->res = NULL;
			US_JLOG_PERROR_RES(err, "acap", "Can't create resampler");
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

	US_JLOG_INFO("acap", "Capture configured on %uHz; capturing ...", acap->pcm_hz);
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
	US_DELETE(acap->dev, snd_pcm_close);
	US_DELETE(acap->dev_params, snd_pcm_hw_params_free);
	US_RING_DELETE_WITH_ITEMS(acap->enc_ring, us_au_encoded_destroy);
	US_RING_DELETE_WITH_ITEMS(acap->pcm_ring, us_au_pcm_destroy);
	if (acap->tids_created) {
		US_JLOG_INFO("acap", "Capture closed");
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
	const us_au_encoded_s *const buf = acap->enc_ring->items[ri];
	if (buf->used == 0 || *size < buf->used) {
		us_ring_consumer_release(acap->enc_ring, ri);
		return US_ERROR_NO_DATA;
	}
	memcpy(data, buf->data, buf->used);
	*size = buf->used;
	*pts = buf->pts;
	us_ring_consumer_release(acap->enc_ring, ri);
	return 0;
}

static void *_pcm_thread(void *v_acap) {
	US_THREAD_SETTLE("us_ac_pcm");

	us_acap_s *const acap = v_acap;
	u8 in[US_AU_MAX_BUF8];

	while (!atomic_load(&acap->stop)) {
		const int frames = snd_pcm_readi(acap->dev, in, acap->pcm_frames);
		if (frames < 0) {
			US_JLOG_PERROR_ALSA(frames, "acap", "Fatal: Can't capture PCM frames");
			break;
		} else if (frames < (int)acap->pcm_frames) {
			US_JLOG_ERROR("acap", "Fatal: Too few PCM frames captured");
			break;
		}

		const int ri = us_ring_producer_acquire(acap->pcm_ring, 0);
		if (ri >= 0) {
			us_au_pcm_s *const out = acap->pcm_ring->items[ri];
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
	US_THREAD_SETTLE("us_ac_enc");

	us_acap_s *const acap = v_acap;
	s16 in_res[US_AU_MAX_BUF16];

	while (!atomic_load(&acap->stop)) {
		const int in_ri = us_ring_consumer_acquire(acap->pcm_ring, 0.1);
		if (in_ri < 0) {
			continue;
		}
		us_au_pcm_s *const in = acap->pcm_ring->items[in_ri];

		s16 *in_ptr;
		if (acap->res != NULL) {
			assert(acap->pcm_hz != US_RTP_OPUS_HZ);
			u32 in_count = acap->pcm_frames;
			u32 out_count = US_AU_HZ_TO_FRAMES(US_RTP_OPUS_HZ);
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
		us_au_encoded_s *const out = acap->enc_ring->items[out_ri];

		const int size = opus_encode(acap->enc, in_ptr, US_AU_HZ_TO_FRAMES(US_RTP_OPUS_HZ), out->data, US_ARRAY_LEN(out->data));
		us_ring_consumer_release(acap->pcm_ring, in_ri);

		if (size > 0) {
			out->used = size;
			out->pts = acap->pts;
			// https://datatracker.ietf.org/doc/html/rfc7587#section-4.2
			acap->pts += US_AU_HZ_TO_FRAMES(US_RTP_OPUS_HZ);
		} else {
			out->used = 0;
			US_JLOG_PERROR_OPUS(size, "acap", "Fatal: Can't encode PCM frame to OPUS");
		}
		us_ring_producer_release(acap->enc_ring, out_ri);
	}

	atomic_store(&acap->stop, true);
	return NULL;
}
