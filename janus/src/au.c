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

#include "uslibs/tools.h"


us_au_pcm_s *us_au_pcm_init(void) {
	us_au_pcm_s *pcm;
	US_CALLOC(pcm, 1);
	return pcm;
}

void us_au_pcm_destroy(us_au_pcm_s *pcm) {
	free(pcm);
}

us_au_encoded_s *us_au_encoded_init(void) {
    us_au_encoded_s *enc;
    US_CALLOC(enc, 1);
    return enc;
}

void us_au_encoded_destroy(us_au_encoded_s *enc) {
	free(enc);
}
