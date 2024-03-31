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


#include "fpsi.h"

#include <stdatomic.h>

#include <pthread.h>

#include "types.h"
#include "tools.h"
#include "threading.h"
#include "logging.h"
#include "frame.h"


us_fpsi_s *us_fpsi_init(const char *name, bool with_meta) {
	us_fpsi_s *fpsi;
	US_CALLOC(fpsi, 1);
	fpsi->name = us_strdup(name);
	fpsi->with_meta = with_meta;
	atomic_init(&fpsi->state_sec_ts, 0);
	atomic_init(&fpsi->state, 0);
	return fpsi;
}

void us_fpsi_destroy(us_fpsi_s *fpsi) {
	free(fpsi->name);
	free(fpsi);
}

void us_fpsi_frame_to_meta(const us_frame_s *frame, us_fpsi_meta_s *meta) {
	meta->width = frame->width;
	meta->height = frame->height;
	meta->online = frame->online;
}

void us_fpsi_update(us_fpsi_s *fpsi, bool bump, const us_fpsi_meta_s *meta) {
	if (meta != NULL) {
		assert(fpsi->with_meta);
	} else {
		assert(!fpsi->with_meta);
	}

	const sll now_sec_ts = us_floor_ms(us_get_now_monotonic());
	if (atomic_load(&fpsi->state_sec_ts) != now_sec_ts) {
		US_LOG_PERF_FPS("FPS: %s: %u", fpsi->name, fpsi->accum);

		// Fast mutex-less store method
		ull state = (ull)fpsi->accum & 0xFFFF;
		if (fpsi->with_meta) {
			assert(meta != NULL);
			state |= (ull)(meta->width & 0xFFFF) << 16;
			state |= (ull)(meta->height & 0xFFFF) << 32;
			state |= (ull)(meta->online ? 1 : 0) << 48;
		}
		atomic_store(&fpsi->state, state); // Сначала инфа
		atomic_store(&fpsi->state_sec_ts, now_sec_ts); // Потом время, это важно
		fpsi->accum = 0;
	}
	if (bump) {
		++fpsi->accum;
	}
}

uint us_fpsi_get(us_fpsi_s *fpsi, us_fpsi_meta_s *meta) {
	if (meta != NULL) {
		assert(fpsi->with_meta);
	} else {
		assert(!fpsi->with_meta);
	}

	// Между чтением инфы и времени может быть гонка,
	// но это неважно. Если время свежее, до данные тоже
	// будут свежмими, обратный случай не так важен.
	const sll now_sec_ts = us_floor_ms(us_get_now_monotonic());
	const sll state_sec_ts = atomic_load(&fpsi->state_sec_ts); // Сначала время
	const ull state = atomic_load(&fpsi->state); // Потом инфа

	uint current = state & 0xFFFF;
	if (fpsi->with_meta) {
		assert(meta != NULL);
		meta->width = (state >> 16) & 0xFFFF;
		meta->height = (state >> 32) & 0xFFFF;
		meta->online = (state >> 48) & 1;
	}

	if (state_sec_ts != now_sec_ts && (state_sec_ts + 1) != now_sec_ts) {
		// Только текущая или прошлая секунда
		current = 0;
	}
	return current;
}
