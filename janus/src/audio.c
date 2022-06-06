/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file is partially based on this code:                       #
#      - https://github.com/catid/kvm/blob/master/kvm_pipeline/src           #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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


#include "audio.h"


#define JLOG_PERROR_ALSA(_err, _prefix, _msg, ...)	JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, snd_strerror(_err))
#define JLOG_PERROR_RES(_err, _prefix, _msg, ...)	JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, speex_resampler_strerror(_err))
#define JLOG_PERROR_OPUS(_err, _prefix, _msg, ...)	JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, opus_strerror(_err))

// A number of frames per 1 channel:
//   - https://github.com/xiph/opus/blob/7b05f44/src/opus_demo.c#L368
#define HZ_TO_FRAMES(_hz)	(6 * (_hz) / 50) // 120ms
#define HZ_TO_BUF16(_hz)	(HZ_TO_FRAMES(_hz) * 2) // One stereo frame = (16bit L) + (16bit R)
#define HZ_TO_BUF8(_hz)		(HZ_TO_BUF16(_hz) * sizeof(int16_t))

#define MIN_PCM_HZ			8000
#define MAX_PCM_HZ			192000
#define MAX_BUF16			HZ_TO_BUF16(MAX_PCM_HZ)
#define MAX_BUF8			HZ_TO_BUF8(MAX_PCM_HZ)
#define ENCODER_INPUT_HZ	48000


typedef struct {
	int16_t		data[MAX_BUF16];
} _pcm_buffer_s;

typedef struct {
	uint8_t		data[MAX_BUF8]; // Worst case
	size_t		used;
	uint64_t	pts;
} _enc_buffer_s;


static void *_pcm_thread(void *v_audio);
static void *_encoder_thread(void *v_audio);


audio_s *audio_init(const char *name, unsigned pcm_hz) {
	audio_s *audio;
	A_CALLOC(audio, 1);
	audio->pcm_hz = pcm_hz;
	audio->pcm_queue = queue_init(8);
	audio->enc_queue = queue_init(8);
	atomic_init(&audio->working, true);

	int err;

	{
		if ((err = snd_pcm_open(&audio->pcm, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			audio->pcm = NULL;
			JLOG_PERROR_ALSA(err, "audio", "Can't open PCM capture");
			goto error;
		}
		assert(!snd_pcm_hw_params_malloc(&audio->pcm_params));

#		define SET_PARAM(_msg, _func, ...) { \
				if ((err = _func(audio->pcm, audio->pcm_params, ##__VA_ARGS__)) < 0) { \
					JLOG_PERROR_ALSA(err, "audio", _msg); \
					goto error; \
				} \
			}

		SET_PARAM("Can't initialize PCM params",	snd_pcm_hw_params_any);
		SET_PARAM("Can't set PCM access type",		snd_pcm_hw_params_set_access, SND_PCM_ACCESS_RW_INTERLEAVED);
		SET_PARAM("Can't set PCM channels numbre",	snd_pcm_hw_params_set_channels, 2);
		SET_PARAM("Can't set PCM sampling format",	snd_pcm_hw_params_set_format, SND_PCM_FORMAT_S16_LE);
		SET_PARAM("Can't set PCM sampling rate",	snd_pcm_hw_params_set_rate_near, &audio->pcm_hz, 0);
		if (audio->pcm_hz < MIN_PCM_HZ || audio->pcm_hz > MAX_PCM_HZ) {
			JLOG_ERROR("audio", "Unsupported PCM freq: %u; should be: %u <= F <= %u",
				audio->pcm_hz, MIN_PCM_HZ, MAX_PCM_HZ);
			goto error;
		}
		JLOG_INFO("audio", "Using PCM freq: %u", audio->pcm_hz);
		audio->pcm_frames = HZ_TO_FRAMES(audio->pcm_hz);
		audio->pcm_size = HZ_TO_BUF8(audio->pcm_hz);
		SET_PARAM("Can't apply PCM params", snd_pcm_hw_params);

#		undef SET_PARAM
	}

	if (audio->pcm_hz != ENCODER_INPUT_HZ) {
		audio->res = speex_resampler_init(2, audio->pcm_hz, ENCODER_INPUT_HZ, SPEEX_RESAMPLER_QUALITY_DESKTOP, &err);
		if (err < 0) {
			audio->res = NULL;
			JLOG_PERROR_RES(err, "audio", "Can't create resampler");
			goto error;
		}
	}

	{
		// OPUS_APPLICATION_VOIP, OPUS_APPLICATION_RESTRICTED_LOWDELAY
		audio->enc = opus_encoder_create(ENCODER_INPUT_HZ, 2, OPUS_APPLICATION_AUDIO, &err);
		assert(err == 0);
		assert(!opus_encoder_ctl(audio->enc, OPUS_SET_BITRATE(48000)));
		assert(!opus_encoder_ctl(audio->enc, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)));
		assert(!opus_encoder_ctl(audio->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)));
		// OPUS_SET_INBAND_FEC(1), OPUS_SET_PACKET_LOSS_PERC(10): see rtpa.c
	}

	JLOG_INFO("audio", "Pipeline prepared; capturing ...");
	audio->tids_created = true;
	A_THREAD_CREATE(&audio->enc_tid, _encoder_thread, audio);
	A_THREAD_CREATE(&audio->pcm_tid, _pcm_thread, audio);

	return audio;

	error:
		audio_destroy(audio);
		return NULL;
}

void audio_destroy(audio_s *audio) {
	if (audio->tids_created) {
		atomic_store(&audio->working, false);
		A_THREAD_JOIN(audio->pcm_tid);
		A_THREAD_JOIN(audio->enc_tid);
	}
	if (audio->enc) {
		opus_encoder_destroy(audio->enc);
	}
	if (audio->res) {
		speex_resampler_destroy(audio->res);
	}
	if (audio->pcm) {
		snd_pcm_close(audio->pcm);
	}
	if (audio->pcm_params) {
		snd_pcm_hw_params_free(audio->pcm_params);
	}
#	define FREE_QUEUE(_suffix) { \
			while (!queue_get_free(audio->_suffix##_queue)) { \
				_##_suffix##_buffer_s *ptr; \
				assert(!queue_get(audio->_suffix##_queue, (void **)&ptr, 1)); \
				free(ptr); \
			} \
			queue_destroy(audio->_suffix##_queue); \
		}
	FREE_QUEUE(enc);
	FREE_QUEUE(pcm);
#	undef FREE_QUEUE
	if (audio->tids_created) {
		JLOG_INFO("audio", "Pipeline closed");
	}
	free(audio);
}

int audio_get_encoded(audio_s *audio, uint8_t *data, size_t *size, uint64_t *pts) {
	if (!atomic_load(&audio->working)) {
		return -1;
	}
	_enc_buffer_s *buf;
	if (!queue_get(audio->enc_queue, (void **)&buf, 1)) {
		if (*size < buf->used) {
			free(buf);
			return -3;
		}
		memcpy(data, buf->data, buf->used);
		*size = buf->used;
		*pts = buf->pts;
		free(buf);
		return 0;
	}
	return -2;
}

static void *_pcm_thread(void *v_audio) {
	A_THREAD_RENAME("us_a_pcm");

	audio_s *audio = (audio_s *)v_audio;
	uint8_t in[MAX_BUF8];

	while (atomic_load(&audio->working)) {
		int frames = snd_pcm_readi(audio->pcm, in, audio->pcm_frames);
		if (frames < 0) {
			JLOG_PERROR_ALSA(frames, "audio", "Can't capture PCM frames; breaking audio ...");
			break;
		} else if (frames < (int)audio->pcm_frames) {
			JLOG_ERROR("audio", "Too few PCM frames captured; breaking audio ...");
			break;
		}

		if (queue_get_free(audio->pcm_queue)) {
			_pcm_buffer_s *out;
			A_CALLOC(out, 1);
			memcpy(out->data, in, audio->pcm_size);
			assert(!queue_put(audio->pcm_queue, out, 1));
		} else {
			JLOG_ERROR("audio", "PCM queue is full");
		}
	}

	atomic_store(&audio->working, false);
	return NULL;
}

static void *_encoder_thread(void *v_audio) {
	A_THREAD_RENAME("us_a_enc");

	audio_s *audio = (audio_s *)v_audio;
	int16_t in_res[MAX_BUF16];

	while (atomic_load(&audio->working)) {
		_pcm_buffer_s *in;
		if (!queue_get(audio->pcm_queue, (void **)&in, 1)) {
			int16_t *in_ptr;
			if (audio->res) {
				assert(audio->pcm_hz != ENCODER_INPUT_HZ);
				uint32_t in_count = audio->pcm_frames;
				uint32_t out_count = HZ_TO_FRAMES(ENCODER_INPUT_HZ);
				speex_resampler_process_interleaved_int(audio->res, in->data, &in_count, in_res, &out_count);
				in_ptr = in_res;
			} else {
				assert(audio->pcm_hz == ENCODER_INPUT_HZ);
				in_ptr = in->data;
			}

			_enc_buffer_s *out;
			A_CALLOC(out, 1);
			int size = opus_encode(audio->enc, in_ptr, HZ_TO_FRAMES(ENCODER_INPUT_HZ), out->data, ARRAY_LEN(out->data));
			free(in);
			if (size < 0) {
				JLOG_PERROR_OPUS(size, "audio", "Can't encode PCM frame to OPUS; breaking audio ...");
				free(out);
				break;
			}
			out->used = size;
			out->pts = audio->pts;
			// https://datatracker.ietf.org/doc/html/rfc7587#section-4.2
			audio->pts += HZ_TO_FRAMES(ENCODER_INPUT_HZ);

			if (queue_get_free(audio->enc_queue)) {
				assert(!queue_put(audio->enc_queue, out, 1));
			} else {
				JLOG_ERROR("audio", "OPUS encoder queue is full");
				free(out);
			}
		}
	}

	atomic_store(&audio->working, false);
	return NULL;
}
