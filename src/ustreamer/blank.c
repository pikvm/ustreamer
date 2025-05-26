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


#include "blank.h"

#include "../libs/types.h"
#include "../libs/tools.h"
#include "../libs/frame.h"
#include "../libs/frametext.h"

#include "encoders/cpu/encoder.h"


us_blank_s *us_blank_init(void) {
	us_blank_s *blank;
	US_CALLOC(blank, 1);
	blank->ft = us_frametext_init();
	blank->raw = blank->ft->frame;
	blank->jpeg = us_frame_init();
	us_blank_draw(blank, "< NO LIVE VIDEO >", 640, 480);
	return blank;
}

void us_blank_draw(us_blank_s *blank, const char *text, uint width, uint height) {
	us_frametext_draw(blank->ft, text, width, height);
	us_cpu_encoder_compress(blank->raw, blank->jpeg, 95);
}

void us_blank_destroy(us_blank_s *blank) {
	us_frame_destroy(blank->jpeg);
	us_frametext_destroy(blank->ft);
	free(blank);
}
