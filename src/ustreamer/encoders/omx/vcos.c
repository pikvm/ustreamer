/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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


#include "vcos.h"


int vcos_my_semwait(const char *prefix, VCOS_SEMAPHORE_T *sem, long double timeout) {
	// vcos_semaphore_wait() can wait infinite
	// vcos_semaphore_wait_timeout() is broken by design:
	//   - https://github.com/pikvm/ustreamer/issues/56
	//   - https://github.com/raspberrypi/userland/issues/658
	//   - The current approach is an ugly busyloop
	// Три стула.

	long double deadline_ts = get_now_monotonic() + timeout;
	VCOS_STATUS_T sem_status;

	while (true) {
		sem_status = vcos_semaphore_trywait(sem);
		if (sem_status == VCOS_SUCCESS) {
			return 0;
		} else if (sem_status != VCOS_EAGAIN || get_now_monotonic() > deadline_ts) {
			break;
		}
		if (usleep(1000) < 0) {
			break;
		}
	}

	switch (sem_status) {
		case VCOS_EAGAIN: LOG_ERROR("%sCan't wait VCOS semaphore: EAGAIN (timeout)", prefix); break;
		case VCOS_EINVAL: LOG_ERROR("%sCan't wait VCOS semaphore: EINVAL", prefix); break;
		default: LOG_ERROR("%sCan't wait VCOS semaphore: %d", prefix, sem_status); break;
	}
	return -1;
}
