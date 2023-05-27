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


#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>

#include <sys/types.h>

#include <pthread.h>
#include <alsa/asoundlib.h>
#include <speex/speex_resampler.h>
#include <opus/opus.h>

#include "uslibs/tools.h"
#include "uslibs/array.h"
#include "uslibs/threading.h"

#include "logging.h"
#include "queue.h"


typedef struct {
	snd_pcm_t			*pcm;
	unsigned			pcm_hz;
	unsigned			pcm_frames;
	size_t				pcm_size;
	snd_pcm_hw_params_t	*pcm_params;
	SpeexResamplerState	*res;
	OpusEncoder			*enc;

	us_queue_s			*pcm_queue;
	us_queue_s			*enc_queue;
	uint32_t			pts;

	pthread_t			pcm_tid;
	pthread_t			enc_tid;
	bool				tids_created;
	atomic_bool			stop;
} us_audio_s;


bool us_audio_probe(const char *name);

us_audio_s *us_audio_init(const char *name, unsigned pcm_hz);
void us_audio_destroy(us_audio_s *audio);

int us_audio_get_encoded(us_audio_s *audio, uint8_t *data, size_t *size, uint64_t *pts);
