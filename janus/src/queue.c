/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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


us_queue_s *us_queue_init(unsigned capacity) {
	us_queue_s *queue;
	US_CALLOC(queue, 1);
	US_CALLOC(queue->items, capacity);
	queue->capacity = capacity;
	US_MUTEX_INIT(queue->mutex);

	pthread_condattr_t attrs;
	assert(!pthread_condattr_init(&attrs));
	assert(!pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC));
	assert(!pthread_cond_init(&queue->full_cond, &attrs));
	assert(!pthread_cond_init(&queue->empty_cond, &attrs));
	assert(!pthread_condattr_destroy(&attrs));
	return queue;
}

void us_queue_destroy(us_queue_s *queue) {
	US_COND_DESTROY(queue->empty_cond);
	US_COND_DESTROY(queue->full_cond);
	US_MUTEX_DESTROY(queue->mutex);
	free(queue->items);
	free(queue);
}

#define _WAIT_OR_UNLOCK(x_var, x_cond) { \
		struct timespec m_ts; \
		assert(!clock_gettime(CLOCK_MONOTONIC, &m_ts)); \
		us_ld_to_timespec(us_timespec_to_ld(&m_ts) + timeout, &m_ts); \
		while (x_var) { \
			const int err = pthread_cond_timedwait(&(x_cond), &queue->mutex, &m_ts); \
			if (err == ETIMEDOUT) { \
				US_MUTEX_UNLOCK(queue->mutex); \
				return -1; \
			} \
			assert(!err); \
		} \
	}

int us_queue_put(us_queue_s *queue, void *item, long double timeout) {
	US_MUTEX_LOCK(queue->mutex);
	if (timeout == 0) {
		if (queue->size == queue->capacity) {
			US_MUTEX_UNLOCK(queue->mutex);
			return -1;
		}
	} else {
		_WAIT_OR_UNLOCK(queue->size == queue->capacity, queue->full_cond);
	}
	queue->items[queue->in] = item;
	++queue->size;
	++queue->in;
	queue->in %= queue->capacity;
	US_MUTEX_UNLOCK(queue->mutex);
	US_COND_BROADCAST(queue->empty_cond);
	return 0;
}

int us_queue_get(us_queue_s *queue, void **item, long double timeout) {
	US_MUTEX_LOCK(queue->mutex);
	_WAIT_OR_UNLOCK(queue->size == 0, queue->empty_cond);
	*item = queue->items[queue->out];
	--queue->size;
	++queue->out;
	queue->out %= queue->capacity;
	US_MUTEX_UNLOCK(queue->mutex);
	US_COND_BROADCAST(queue->full_cond);
	return 0;
}

#undef _WAIT_OR_UNLOCK

int us_queue_get_free(us_queue_s *queue) {
	US_MUTEX_LOCK(queue->mutex);
	const unsigned size = queue->size;
	US_MUTEX_UNLOCK(queue->mutex);
	return queue->capacity - size;
}
