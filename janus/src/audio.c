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


#include "audio.h"


#define _JLOG_PERROR_ALSA(_err, _prefix, _msg, ...)	US_JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, snd_strerror(_err))
#define _JLOG_PERROR_RES(_err, _prefix, _msg, ...)	US_JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, speex_resampler_strerror(_err))
#define _JLOG_PERROR_OPUS(_err, _prefix, _msg, ...)	US_JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, opus_strerror(_err))

// A number of frames per 1 channel:
//   - https://github.com/xiph/opus/blob/7b05f44/src/opus_demo.c#L368
#define _HZ_TO_FRAMES(_hz)	(6 * (_hz) / 50) // 120ms
#define _HZ_TO_BUF16(_hz)	(_HZ_TO_FRAMES(_hz) * 2) // One stereo frame = (16bit L) + (16bit R)
#define _HZ_TO_BUF8(_hz)	(_HZ_TO_BUF16(_hz) * sizeof(int16_t))

#define _MIN_PCM_HZ			8000
#define _MAX_PCM_HZ			192000
#define _MAX_BUF16			_HZ_TO_BUF16(_MAX_PCM_HZ)
#define _MAX_BUF8			_HZ_TO_BUF8(_MAX_PCM_HZ)
#define _ENCODER_INPUT_HZ	48000


typedef struct {
	int16_t		data[_MAX_BUF16];
} _pcm_buffer_s;

typedef struct {
	uint8_t		data[_MAX_BUF8]; // Worst case
	size_t		used;
	uint64_t	pts;
} _enc_buffer_s;


static void *_pcm_thread(void *v_audio);
static void *_encoder_thread(void *v_audio);


bool us_audio_probe(const char *name) {
	snd_pcm_t *pcm;
	int err;
	US_JLOG_INFO("audio", "Probing PCM capture ...");
	if ((err = snd_pcm_open(&pcm, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		_JLOG_PERROR_ALSA(err, "audio", "Can't probe PCM capture");
		return false;
	}
	snd_pcm_close(pcm);
	US_JLOG_INFO("audio", "PCM capture is available");
	return true;
}

us_audio_s *us_audio_init(const char *name, unsigned pcm_hz) {
	us_audio_s *audio;
	US_CALLOC(audio, 1);
	audio->pcm_hz = pcm_hz;
	audio->pcm_queue = us_queue_init(8);
	audio->enc_queue = us_queue_init(8);
	atomic_init(&audio->stop, false);

	int err;

	{
		if ((err = snd_pcm_open(&audio->pcm, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			audio->pcm = NULL;
			_JLOG_PERROR_ALSA(err, "audio", "Can't open PCM capture");
			goto error;
		}
		assert(!snd_pcm_hw_params_malloc(&audio->pcm_params));

#		define SET_PARAM(_msg, _func, ...) { \
				if ((err = _func(audio->pcm, audio->pcm_params, ##__VA_ARGS__)) < 0) { \
					_JLOG_PERROR_ALSA(err, "audio", _msg); \
					goto error; \
				} \
			}

		SET_PARAM("Can't initialize PCM params",	snd_pcm_hw_params_any);
		SET_PARAM("Can't set PCM access type",		snd_pcm_hw_params_set_access, SND_PCM_ACCESS_RW_INTERLEAVED);
		SET_PARAM("Can't set PCM channels numbre",	snd_pcm_hw_params_set_channels, 2);
		SET_PARAM("Can't set PCM sampling format",	snd_pcm_hw_params_set_format, SND_PCM_FORMAT_S16_LE);
		SET_PARAM("Can't set PCM sampling rate",	snd_pcm_hw_params_set_rate_near, &audio->pcm_hz, 0);
		if (audio->pcm_hz < _MIN_PCM_HZ || audio->pcm_hz > _MAX_PCM_HZ) {
			US_JLOG_ERROR("audio", "Unsupported PCM freq: %u; should be: %u <= F <= %u",
				audio->pcm_hz, _MIN_PCM_HZ, _MAX_PCM_HZ);
			goto error;
		}
		audio->pcm_frames = _HZ_TO_FRAMES(audio->pcm_hz);
		audio->pcm_size = _HZ_TO_BUF8(audio->pcm_hz);
		SET_PARAM("Can't apply PCM params", snd_pcm_hw_params);

#		undef SET_PARAM
	}

	if (audio->pcm_hz != _ENCODER_INPUT_HZ) {
		audio->res = speex_resampler_init(2, audio->pcm_hz, _ENCODER_INPUT_HZ, SPEEX_RESAMPLER_QUALITY_DESKTOP, &err);
		if (err < 0) {
			audio->res = NULL;
			_JLOG_PERROR_RES(err, "audio", "Can't create resampler");
			goto error;
		}
	}

	{
		// OPUS_APPLICATION_VOIP, OPUS_APPLICATION_RESTRICTED_LOWDELAY
		audio->enc = opus_encoder_create(_ENCODER_INPUT_HZ, 2, OPUS_APPLICATION_AUDIO, &err);
		assert(err == 0);
		assert(!opus_encoder_ctl(audio->enc, OPUS_SET_BITRATE(48000)));
		assert(!opus_encoder_ctl(audio->enc, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)));
		assert(!opus_encoder_ctl(audio->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)));
		// OPUS_SET_INBAND_FEC(1), OPUS_SET_PACKET_LOSS_PERC(10): see rtpa.c
	}

	US_JLOG_INFO("audio", "Pipeline configured on %uHz; capturing ...", audio->pcm_hz);
	audio->tids_created = true;
	US_THREAD_CREATE(audio->enc_tid, _encoder_thread, audio);
	US_THREAD_CREATE(audio->pcm_tid, _pcm_thread, audio);

	return audio;

	error:
		us_audio_destroy(audio);
		return NULL;
}

void us_audio_destroy(us_audio_s *audio) {
	if (audio->tids_created) {
		atomic_store(&audio->stop, true);
		US_THREAD_JOIN(audio->pcm_tid);
		US_THREAD_JOIN(audio->enc_tid);
	}
	US_DELETE(audio->enc, opus_encoder_destroy);
	US_DELETE(audio->res, speex_resampler_destroy);
	US_DELETE(audio->pcm, snd_pcm_close);
	US_DELETE(audio->pcm_params, snd_pcm_hw_params_free);
	US_QUEUE_DELETE_WITH_ITEMS(audio->enc_queue, free);
	US_QUEUE_DELETE_WITH_ITEMS(audio->pcm_queue, free);
	if (audio->tids_created) {
		US_JLOG_INFO("audio", "Pipeline closed");
	}
	free(audio);
}

int us_audio_get_encoded(us_audio_s *audio, uint8_t *data, size_t *size, uint64_t *pts) {
	if (atomic_load(&audio->stop)) {
		return -1;
	}
	_enc_buffer_s *buf;
	if (!us_queue_get(audio->enc_queue, (void **)&buf, 0.1)) {
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
	US_THREAD_RENAME("us_a_pcm");

	us_audio_s *const audio = (us_audio_s *)v_audio;
	uint8_t in[_MAX_BUF8];

	while (!atomic_load(&audio->stop)) {
		const int frames = snd_pcm_readi(audio->pcm, in, audio->pcm_frames);
		if (frames < 0) {
			_JLOG_PERROR_ALSA(frames, "audio", "Fatal: Can't capture PCM frames");
			break;
		} else if (frames < (int)audio->pcm_frames) {
			US_JLOG_ERROR("audio", "Fatal: Too few PCM frames captured");
			break;
		}

		if (us_queue_get_free(audio->pcm_queue)) {
			_pcm_buffer_s *out;
			US_CALLOC(out, 1);
			memcpy(out->data, in, audio->pcm_size);
			assert(!us_queue_put(audio->pcm_queue, out, 0));
		} else {
			US_JLOG_ERROR("audio", "PCM queue is full");
		}
	}

	atomic_store(&audio->stop, true);
	return NULL;
}

static void *_encoder_thread(void *v_audio) {
	US_THREAD_RENAME("us_a_enc");

	us_audio_s *const audio = (us_audio_s *)v_audio;
	int16_t in_res[_MAX_BUF16];

	while (!atomic_load(&audio->stop)) {
		_pcm_buffer_s *in;
		if (!us_queue_get(audio->pcm_queue, (void **)&in, 0.1)) {
			int16_t *in_ptr;
			if (audio->res != NULL) {
				assert(audio->pcm_hz != _ENCODER_INPUT_HZ);
				uint32_t in_count = audio->pcm_frames;
				uint32_t out_count = _HZ_TO_FRAMES(_ENCODER_INPUT_HZ);
				speex_resampler_process_interleaved_int(audio->res, in->data, &in_count, in_res, &out_count);
				in_ptr = in_res;
			} else {
				assert(audio->pcm_hz == _ENCODER_INPUT_HZ);
				in_ptr = in->data;
			}

			_enc_buffer_s *out;
			US_CALLOC(out, 1);
			const int size = opus_encode(audio->enc, in_ptr, _HZ_TO_FRAMES(_ENCODER_INPUT_HZ), out->data, US_ARRAY_LEN(out->data));
			free(in);
			if (size < 0) {
				_JLOG_PERROR_OPUS(size, "audio", "Fatal: Can't encode PCM frame to OPUS");
				free(out);
				break;
			}
			out->used = size;
			out->pts = audio->pts;
			// https://datatracker.ietf.org/doc/html/rfc7587#section-4.2
			audio->pts += _HZ_TO_FRAMES(_ENCODER_INPUT_HZ);

			if (us_queue_put(audio->enc_queue, out, 0) != 0) {
				US_JLOG_ERROR("audio", "OPUS encoder queue is full");
				free(out);
			}
		}
	}

	atomic_store(&audio->stop, true);
	return NULL;
}
