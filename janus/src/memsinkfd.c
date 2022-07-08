/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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


int memsink_fd_wait_frame(int fd, memsink_shared_s* mem, uint64_t last_id) {
	long double deadline_ts = get_now_monotonic() + 1; // wait_timeout
	long double now;
	do {
		int result = flock_timedwait_monotonic(fd, 1); // lock_timeout
		now = get_now_monotonic();
		if (result < 0 && errno != EWOULDBLOCK) {
			JLOG_PERROR("video", "Can't lock memsink");
			return -1;
		} else if (result == 0) {
			if (mem->magic == MEMSINK_MAGIC && mem->version == MEMSINK_VERSION && mem->id != last_id) {
				return 0;
			}
			if (flock(fd, LOCK_UN) < 0) {
				JLOG_PERROR("video", "Can't unlock memsink");
				return -1;
			}
		}
		usleep(1000); // lock_polling
	} while (now < deadline_ts);
	return -2;
}

int memsink_fd_get_frame(int fd, memsink_shared_s *mem, frame_s *frame, uint64_t *frame_id) {
	frame_set_data(frame, mem->data, mem->used);
	FRAME_COPY_META(mem, frame);
	*frame_id = mem->id;
	mem->last_client_ts = get_now_monotonic();
	int retval = 0;
	if (frame->format != V4L2_PIX_FMT_H264) {
		JLOG_ERROR("video", "Got non-H264 frame from memsink");
		retval = -1;
	}
	if (flock(fd, LOCK_UN) < 0) {
		JLOG_PERROR("video", "Can't unlock memsink");
		retval = -1;
	}
	return retval;
}
