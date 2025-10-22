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


#include "au.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/stat.h>

#include "uslibs/tools.h"


bool us_au_probe(const char *name) {
	// This function is very limited. It takes something like:
	//   hw:0,0 or hw:tc358743,0 or plughw:UAC2Gadget,0
	// parses card name (0, tc358743, UAC2Gadget) and checks
	// the existence of it in /proc/asound/.
	// It's enough for our case.

	if (name == NULL) {
		return false;
	}

	if (strchr(name, '/') || strchr(name, '.')) {
		return false;
	}

	const char *begin = strchr(name, ':');
	if (begin == NULL) {
		return false;
	}
	begin += 1;
	if (*begin == '\0') {
		return false;
	}

	const char *end = strchr(begin, ',');
	if (end == NULL) {
		return false;
	}
	if (end - begin < 1) {
		return false;
	}

	char *card = us_strdup(begin);
	card[end - begin] = '\0';

	bool numeric = true;
	for (uz index = 0; card[index] != '\0'; ++index) {
		if (!isdigit(card[index])) {
			numeric = false;
			break;
		}
	}

	char *path;
	if (numeric) {
		US_ASPRINTF(path, "/proc/asound/card%s", card);
	} else {
		US_ASPRINTF(path, "/proc/asound/%s", card);
	}

	bool ok = false;
	struct stat st;
	if (lstat(path, &st) == 0) {
		if (numeric && S_ISDIR(st.st_mode)) {
			ok = true;
		} else if (!numeric && S_ISLNK(st.st_mode)) {
			ok = true;
		}
	}
	free(path);
	free(card);
	return ok;
}

us_au_pcm_s *us_au_pcm_init(void) {
	us_au_pcm_s *pcm;
	US_CALLOC(pcm, 1);
	return pcm;
}

void us_au_pcm_destroy(us_au_pcm_s *pcm) {
	free(pcm);
}

void us_au_pcm_mix(us_au_pcm_s *dest, us_au_pcm_s *src) {
	const uz size = src->frames * US_RTP_OPUS_CH * 2; // 2 for 16 bit
	if (src->frames == 0) {
		return;
	} else if (dest->frames == 0) {
		memcpy(dest->data, src->data, size);
		dest->frames = src->frames;
	} else if (dest->frames == src->frames) {
		// https://stackoverflow.com/questions/12089662
		for (uz index = 0; index < size; ++index) {
			int a = dest->data[index];
			int b = src->data[index];
			int m;

			a += 32768;
			b += 32768;

			if ((a < 32768) && (b < 32768)) {
				m = a * b / 32768;
			} else {
				m = 2 * (a + b) - (a * b) / 32768 - 65536;
			}
			if (m == 65536) {
				m = 65535;
			}
			m -= 32768;

			dest->data[index] = m;
		}
	}
}

us_au_encoded_s *us_au_encoded_init(void) {
	us_au_encoded_s *enc;
	US_CALLOC(enc, 1);
	return enc;
}

void us_au_encoded_destroy(us_au_encoded_s *enc) {
	free(enc);
}
