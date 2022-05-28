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
#define JLOG_PERROR_OPUS(_err, _prefix, _msg, ...)	JLOG_ERROR(_prefix, _msg ": %s", ##__VA_ARGS__, opus_strerror(_err))


// https://github.com/xiph/opus/blob/7b05f44/src/opus_demo.c#L368
#define PCM_BITRATE		48000
#define PCM_FRAMES		(6 * PCM_BITRATE / 50) // 120ms
#define PCM_DATA_SIZE	(PCM_FRAMES * 2)
#define RAW_DATA_SIZE	(PCM_DATA_SIZE * sizeof(opus_int16))


typedef struct {
	opus_int16	data[PCM_DATA_SIZE];
} _pcm_buf_s;

typedef struct {
	uint8_t		data[RAW_DATA_SIZE]; // Worst
	size_t		used;
	uint64_t	pts;
} _enc_buf_s;


static void *_pcm_thread(void *v_audio);
static void *_encoder_thread(void *v_audio);


audio_s *audio_init(const char *name) {
	audio_s *audio;
	A_CALLOC(audio, 1);
	audio->pcm_queue = queue_init(8);
	audio->enc_queue = queue_init(8);
	atomic_init(&audio->run, true);

	{
		int err;
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
		unsigned pcm_bitrate = PCM_BITRATE;
		SET_PARAM("Can't set PCM sampling rate",	snd_pcm_hw_params_set_rate_near, &pcm_bitrate, 0);
		if (pcm_bitrate != PCM_BITRATE) {
			JLOG_ERROR("audio", "PCM bitrate mismatch: %u, should be %u", pcm_bitrate, PCM_BITRATE);
			goto error;
		}
		SET_PARAM("Can't apply PCM params",			snd_pcm_hw_params);

#		undef SET_PARAM
	}

	{
		int err;
		// OPUS_APPLICATION_VOIP
		// OPUS_APPLICATION_RESTRICTED_LOWDELAY
		audio->enc = opus_encoder_create(PCM_BITRATE, 2, OPUS_APPLICATION_AUDIO, &err);
		if (err < 0) {
			audio->enc = NULL;
			JLOG_PERROR_OPUS(err, "audio", "Can't create OPUS encoder");
			goto error;
		}

#		define SET_PARAM(_msg, _ctl) { \
				if ((err = opus_encoder_ctl(audio->enc, _ctl)) < 0) { \
					JLOG_PERROR_OPUS(err, "audio", _msg); \
					goto error; \
				} \
			}

		SET_PARAM("Can't set OPUS bitrate",			OPUS_SET_BITRATE(48000));
		SET_PARAM("Can't set OPUS max bandwidth",	OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
		SET_PARAM("Can't set OPUS signal type",		OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
		// Also see rtpa.c
		// SET_PARAM("Can't set OPUS FEC",			OPUS_SET_INBAND_FEC(1));
		// SET_PARAM("Can't set OPUS exploss",		OPUS_SET_PACKET_LOSS_PERC(10));

#		undef SET_PARAM
	}

	JLOG_INFO("audio", "PCM & OPUS prepared; capturing ...");
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
		atomic_store(&audio->run, false);
		A_THREAD_JOIN(audio->pcm_tid);
		A_THREAD_JOIN(audio->enc_tid);
	}
	if (audio->enc) {
		opus_encoder_destroy(audio->enc);
	}
	if (audio->pcm) {
		snd_pcm_close(audio->pcm);
	}
	if (audio->pcm_params) {
		snd_pcm_hw_params_free(audio->pcm_params);
	}
#	define FREE_QUEUE(_suffix) { \
			while (!queue_get_free(audio->_suffix##_queue)) { \
				_##_suffix##_buf_s *ptr; \
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

int audio_copy_encoded(audio_s *audio, uint8_t *data, size_t *size, uint64_t *pts) {
	if (!atomic_load(&audio->run)) {
		return -1;
	}
	_enc_buf_s *in;
	if (!queue_get(audio->enc_queue, (void **)&in, 1)) {
		if (*size < in->used) {
			free(in);
			return -3;
		}
		memcpy(data, in->data, in->used);
		*size = in->used;
		*pts = in->pts;
		free(in);
		return 0;
	}
	return -2;
}

static void *_pcm_thread(void *v_audio) {
	A_THREAD_RENAME("us_a_pcm");

	audio_s *audio = (audio_s *)v_audio;

	while (atomic_load(&audio->run)) {
		uint8_t in[RAW_DATA_SIZE];
		int frames = snd_pcm_readi(audio->pcm, in, PCM_FRAMES);
		if (frames < 0) {
			JLOG_PERROR_ALSA(frames, "audio", "Can't capture PCM frames; breaking audio ...");
			break;
		} else if (frames < PCM_FRAMES) {
			JLOG_ERROR("audio", "Too few PCM frames captured; breaking audio ...");
			break;
		}

		if (queue_get_free(audio->pcm_queue)) {
			_pcm_buf_s *out;
			A_CALLOC(out, 1);
			/*for (unsigned index = 0; index < RAW_DATA_SIZE; ++index) {
				out->data[index] = (opus_int16)in[index * 2 + 1] << 8 | in[index * 2];
			}*/
			memcpy(out->data, in, RAW_DATA_SIZE);
			assert(!queue_put(audio->pcm_queue, out, 1));
		} else {
			JLOG_ERROR("audio", "PCM queue is full");
		}
	}

	atomic_store(&audio->run, false);
	return NULL;
}

static void *_encoder_thread(void *v_audio) {
	A_THREAD_RENAME("us_a_enc");

	audio_s *audio = (audio_s *)v_audio;

	while (atomic_load(&audio->run)) {
		_pcm_buf_s *in;
		if (!queue_get(audio->pcm_queue, (void **)&in, 1)) {
			_enc_buf_s *out;
			A_CALLOC(out, 1);
			int size = opus_encode(audio->enc, in->data, PCM_FRAMES, out->data, RAW_DATA_SIZE);
			free(in);
			if (size < 0) {
				JLOG_PERROR_OPUS(size, "audio", "Can't encode PCM frame to OPUS; breaking audio ...");
				free(out);
				break;
			}
			out->used = size;
			out->pts = audio->pts;
			audio->pts += PCM_FRAMES;

			if (queue_get_free(audio->enc_queue)) {
				assert(!queue_put(audio->enc_queue, out, 1));
			} else {
				JLOG_ERROR("audio", "OPUS encoder queue is full");
				free(out);
			}
		}
	}

	atomic_store(&audio->run, false);
	return NULL;
}
