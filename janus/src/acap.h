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


#pragma once

#include <stdatomic.h>

#include <pthread.h>
#include <alsa/asoundlib.h>
#include <speex/speex_resampler.h>
#include <opus/opus.h>

#include "uslibs/types.h"
#include "uslibs/ring.h"


typedef struct {
	snd_pcm_t			*dev;
	uint				pcm_hz;
	uint				pcm_frames;
	uz					pcm_size;
	snd_pcm_hw_params_t	*dev_params;
	SpeexResamplerState	*res;
	OpusEncoder			*enc;

	us_ring_s		*pcm_ring;
	us_ring_s		*enc_ring;
	u32				pts;

	pthread_t		pcm_tid;
	pthread_t		enc_tid;
	bool			tids_created;
	atomic_bool		stop;
} us_acap_s;


bool us_acap_probe(const char *name);

us_acap_s *us_acap_init(const char *name, uint pcm_hz);
void us_acap_destroy(us_acap_s *acap);

int us_acap_get_encoded(us_acap_s *acap, u8 *data, uz *size, u64 *pts);
