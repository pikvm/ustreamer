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

#include "uslibs/types.h"

#include "rtp.h"

// A number of frames per 1 channel:
//   - https://github.com/xiph/opus/blob/7b05f44/src/opus_demo.c#L368
#define US_AU_FRAME_MS			20
// #define _HZ_TO_FRAMES(_hz)	(6 * (_hz) / 50) // 120ms
#define US_AU_HZ_TO_FRAMES(_hz)	((_hz) / 50) // 20ms
#define US_AU_HZ_TO_BUF16(_hz)	(US_AU_HZ_TO_FRAMES(_hz) * US_RTP_OPUS_CH) // ... * 2: One stereo frame = (16bit L) + (16bit R)
#define US_AU_HZ_TO_BUF8(_hz)	(US_AU_HZ_TO_BUF16(_hz) * sizeof(s16))

#define US_AU_MIN_PCM_HZ		8000
#define US_AU_MAX_PCM_HZ		192000
#define US_AU_MAX_BUF16			US_AU_HZ_TO_BUF16(US_AU_MAX_PCM_HZ)
#define US_AU_MAX_BUF8			US_AU_HZ_TO_BUF8(US_AU_MAX_PCM_HZ)


typedef struct {
	s16		data[US_AU_MAX_BUF16];
	uz		frames;
} us_au_pcm_s;

typedef struct {
	u8		data[US_RTP_PAYLOAD_SIZE];
	uz		used;
	u64		pts;
} us_au_encoded_s;


us_au_pcm_s *us_au_pcm_init(void);
void us_au_pcm_destroy(us_au_pcm_s *pcm);
void us_au_pcm_mix(us_au_pcm_s *a, us_au_pcm_s *b);

us_au_encoded_s *us_au_encoded_init(void);
void us_au_encoded_destroy(us_au_encoded_s *enc);
