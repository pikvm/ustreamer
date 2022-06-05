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


#include "queue.h"


queue_s *queue_init(unsigned capacity) {
	queue_s *queue;
	A_CALLOC(queue, 1);
	A_CALLOC(queue->items, capacity);
	queue->capacity = capacity;
	A_MUTEX_INIT(&queue->mutex);

	pthread_condattr_t attrs;
	assert(!pthread_condattr_init(&attrs));
	assert(!pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC));
	assert(!pthread_cond_init(&queue->full_cond, &attrs));
	assert(!pthread_cond_init(&queue->empty_cond, &attrs));
	assert(!pthread_condattr_destroy(&attrs));
	return queue;
}

void queue_destroy(queue_s *queue) {
	free(queue->items);
	free(queue);
}

#define WAIT_OR_UNLOCK(_var, _cond) { \
		struct timespec ts; \
		assert(!clock_gettime(CLOCK_MONOTONIC, &ts)); \
		ts.tv_sec += timeout; \
		while (_var) { \
			int err = pthread_cond_timedwait(_cond, &queue->mutex, &ts); \
			if (err == ETIMEDOUT) { \
				A_MUTEX_UNLOCK(&queue->mutex); \
				return -1; \
			} \
			assert(!err); \
		} \
	}

int queue_put(queue_s *queue, void *item, unsigned timeout) {
	A_MUTEX_LOCK(&queue->mutex);
	WAIT_OR_UNLOCK(queue->size == queue->capacity, &queue->full_cond);
	queue->items[queue->in] = item;
	++queue->size;
	++queue->in;
	queue->in %= queue->capacity;
	A_MUTEX_UNLOCK(&queue->mutex);
	assert(!pthread_cond_broadcast(&queue->empty_cond));
	return 0;
}

int queue_get(queue_s *queue, void **item, unsigned timeout) {
	A_MUTEX_LOCK(&queue->mutex);
	WAIT_OR_UNLOCK(queue->size == 0, &queue->empty_cond);
	*item = queue->items[queue->out];
	--queue->size;
	++queue->out;
	queue->out %= queue->capacity;
	A_MUTEX_UNLOCK(&queue->mutex);
	assert(!pthread_cond_broadcast(&queue->full_cond));
	return 0;
}

#undef WAIT_OR_UNLOCK

int queue_get_free(queue_s *queue) {
	A_MUTEX_LOCK(&queue->mutex);
	unsigned size = queue->size;
	A_MUTEX_UNLOCK(&queue->mutex);
	return queue->capacity - size;
}
