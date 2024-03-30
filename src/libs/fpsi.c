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
	atomic_init(&fpsi->ts, 0);
	atomic_init(&fpsi->current, 0);
	if (with_meta) {
		US_MUTEX_INIT(fpsi->mutex);
	}
	return fpsi;
}

void us_fpsi_destroy(us_fpsi_s *fpsi) {
	if (fpsi->with_meta) {
		US_MUTEX_DESTROY(fpsi->mutex);
	}
	free(fpsi->name);
	free(fpsi);
}

void us_fpsi_bump(us_fpsi_s *fpsi, const us_frame_s *frame) {
	if (frame != NULL) {
		assert(fpsi->with_meta);
	} else {
		assert(!fpsi->with_meta);
	}

	if (fpsi->with_meta) {
		US_MUTEX_LOCK(fpsi->mutex);
	}

	const sll now_sec_ts = us_floor_ms(us_get_now_monotonic());
	if (atomic_load(&fpsi->ts) != now_sec_ts) {
		US_LOG_PERF_FPS("FPS: %s: %u", fpsi->name, fpsi->accum);
		atomic_store(&fpsi->current, fpsi->accum);
		atomic_store(&fpsi->ts, now_sec_ts);
		fpsi->accum = 0;
	}
	++fpsi->accum;

	if (fpsi->with_meta) {
		assert(frame != NULL);
		US_FRAME_COPY_META(frame, &fpsi->meta);
		US_MUTEX_UNLOCK(fpsi->mutex);
	}
}

uint us_fpsi_get(us_fpsi_s *fpsi, us_fpsi_meta_s *meta) {
	if (meta != NULL) {
		assert(fpsi->with_meta);
	} else {
		assert(!fpsi->with_meta);
	}

	if (fpsi->with_meta) {
		US_MUTEX_LOCK(fpsi->mutex);
	}

	const sll now_sec_ts = us_floor_ms(us_get_now_monotonic());
	const uint current = (atomic_load(&fpsi->ts) == now_sec_ts ? fpsi->current : 0);

	if (fpsi->with_meta) {
		assert(meta != NULL);
		US_FRAME_COPY_META(&fpsi->meta, meta);
		US_MUTEX_UNLOCK(fpsi->mutex);
	}
	return current;
}
