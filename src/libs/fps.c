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


#include "fps.h"

#include "types.h"
#include "tools.h"
#include "logging.h"


us_fps_s *us_fps_init(const char *name) {
	us_fps_s *fps;
	US_CALLOC(fps, 1);
	fps->name = us_strdup(name);
	return fps;
}

void us_fps_destroy(us_fps_s *fps) {
	free(fps->name);
	free(fps);
}

void us_fps_bump(us_fps_s *fps) {
	const sll now_sec_ts = us_floor_ms(us_get_now_monotonic());
	if (now_sec_ts != fps->ts) {
		US_LOG_PERF_FPS("FPS: %s: %u", fps->name, fps->accum);
		atomic_store(&fps->current, fps->accum);
		fps->accum = 0;
		fps->ts = now_sec_ts;
	}
	++fps->accum;
}

void us_fps_reset(us_fps_s *fps) {
	us_fps_bump(fps); // Just show the log record
	fps->accum = 0;
}

uint us_fps_get(us_fps_s *fps) {
	return atomic_load(&fps->current);
}
