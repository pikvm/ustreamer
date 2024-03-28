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


#include "memsinkfd.h"

#include <unistd.h>

#include <linux/videodev2.h>

#include "uslibs/types.h"
#include "uslibs/errors.h"
#include "uslibs/tools.h"
#include "uslibs/frame.h"
#include "uslibs/memsinksh.h"

#include "logging.h"


int us_memsink_fd_wait_frame(int fd, us_memsink_shared_s *mem, u64 last_id) {
	const ldf deadline_ts = us_get_now_monotonic() + 1; // wait_timeout
	ldf now_ts;
	do {
		const int result = us_flock_timedwait_monotonic(fd, 1); // lock_timeout
		now_ts = us_get_now_monotonic();
		if (result < 0 && errno != EWOULDBLOCK) {
			US_JLOG_PERROR("video", "Can't lock memsink");
			return -1;
		} else if (result == 0) {
			if (mem->magic == US_MEMSINK_MAGIC && mem->version == US_MEMSINK_VERSION && mem->id != last_id) {
				return 0;
			}
			if (flock(fd, LOCK_UN) < 0) {
				US_JLOG_PERROR("video", "Can't unlock memsink");
				return -1;
			}
		}
		usleep(1000); // lock_polling
	} while (now_ts < deadline_ts);
	return US_ERROR_NO_DATA;
}

int us_memsink_fd_get_frame(int fd, us_memsink_shared_s *mem, us_frame_s *frame, u64 *frame_id, bool key_required) {
	us_frame_set_data(frame, us_memsink_get_data(mem), mem->used);
	US_FRAME_COPY_META(mem, frame);
	*frame_id = mem->id;
	mem->last_client_ts = us_get_now_monotonic();
	if (key_required) {
		mem->key_requested = true;
	}

	bool retval = 0;
	if (frame->format != V4L2_PIX_FMT_H264) {
		US_JLOG_ERROR("video", "Got non-H264 frame from memsink");
		retval = -1;
	}
	if (flock(fd, LOCK_UN) < 0) {
		US_JLOG_PERROR("video", "Can't unlock memsink");
		retval = -1;
	}
	return retval;
}
