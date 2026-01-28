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


#include "queue.h"

#include <errno.h>
#include <time.h>

#include <pthread.h>

#include "types.h"
#include "tools.h"
#include "threading.h"


us_queue_s *us_queue_init(uint capacity) {
	us_queue_s *q;
	US_CALLOC(q, 1);
	US_CALLOC(q->items, capacity);
	q->capacity = capacity;
	US_MUTEX_INIT(q->mutex);

	pthread_condattr_t attrs;
	US_A(!pthread_condattr_init(&attrs));
	US_A(!pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC));
	US_A(!pthread_cond_init(&q->full_cond, &attrs));
	US_A(!pthread_cond_init(&q->empty_cond, &attrs));
	US_A(!pthread_condattr_destroy(&attrs));
	return q;
}

void us_queue_destroy(us_queue_s *q) {
	US_COND_DESTROY(q->empty_cond);
	US_COND_DESTROY(q->full_cond);
	US_MUTEX_DESTROY(q->mutex);
	free(q->items);
	free(q);
}

#define _WAIT_OR_UNLOCK(x_var, x_cond) { \
		struct timespec m_ts; \
		US_A(!clock_gettime(CLOCK_MONOTONIC, &m_ts)); \
		us_ld_to_timespec(us_timespec_to_ld(&m_ts) + timeout, &m_ts); \
		while (x_var) { \
			const int err = pthread_cond_timedwait(&(x_cond), &q->mutex, &m_ts); \
			if (err == ETIMEDOUT) { \
				US_MUTEX_UNLOCK(q->mutex); \
				return -1; \
			} \
			US_A(!err); \
		} \
	}

int us_queue_put(us_queue_s *q, void *item, ldf timeout) {
	US_MUTEX_LOCK(q->mutex);
	if (timeout == 0) {
		if (q->size == q->capacity) {
			US_MUTEX_UNLOCK(q->mutex);
			return -1;
		}
	} else {
		_WAIT_OR_UNLOCK(q->size == q->capacity, q->full_cond);
	}
	q->items[q->in] = item;
	++q->size;
	++q->in;
	q->in %= q->capacity;
	US_MUTEX_UNLOCK(q->mutex);
	US_COND_BROADCAST(q->empty_cond);
	return 0;
}

int us_queue_get(us_queue_s *q, void **item, ldf timeout) {
	US_MUTEX_LOCK(q->mutex);
	_WAIT_OR_UNLOCK(q->size == 0, q->empty_cond);
	*item = q->items[q->out];
	--q->size;
	++q->out;
	q->out %= q->capacity;
	US_MUTEX_UNLOCK(q->mutex);
	US_COND_BROADCAST(q->full_cond);
	return 0;
}

#undef _WAIT_OR_UNLOCK

bool us_queue_is_empty(us_queue_s *q) {
	US_MUTEX_LOCK(q->mutex);
	const uint size = q->size;
	US_MUTEX_UNLOCK(q->mutex);
	return (bool)(q->capacity - size);
}
