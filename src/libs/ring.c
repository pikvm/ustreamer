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


#include <assert.h>

#include "ring.h"

#include "types.h"
#include "tools.h"
#include "queue.h"


int _acquire(us_ring_s *ring, us_queue_s *queue, ldf timeout);
void _release(us_ring_s *ring, us_queue_s *queue, uint index);


us_ring_s *us_ring_init(uint capacity) {
	us_ring_s *ring;
	US_CALLOC(ring, 1);
	US_CALLOC(ring->items, capacity);
	US_CALLOC(ring->places, capacity);
	ring->capacity = capacity;
	ring->producer = us_queue_init(capacity);
	ring->consumer = us_queue_init(capacity);
	for (uint index = 0; index < capacity; ++index) {
		ring->places[index] = index; // XXX: Just to avoid casting between pointer and uint
		assert(!us_queue_put(ring->producer, (void*)(ring->places + index), 0));
	}
	return ring;
}

void us_ring_destroy(us_ring_s *ring) {
	us_queue_destroy(ring->consumer);
	us_queue_destroy(ring->producer);
	free(ring->places);
	free(ring->items);
	free(ring);
}

int us_ring_producer_acquire(us_ring_s *ring, ldf timeout) {
	return _acquire(ring, ring->producer, timeout);
}

void us_ring_producer_release(us_ring_s *ring, uint index) {
	_release(ring, ring->consumer, index);
}

int us_ring_consumer_acquire(us_ring_s *ring, ldf timeout) {
	return _acquire(ring, ring->consumer, timeout);
}

void us_ring_consumer_release(us_ring_s *ring, uint index) {
	_release(ring, ring->producer, index);
}

int _acquire(us_ring_s *ring, us_queue_s *queue, ldf timeout) {
	(void)ring;
	uint *place;
	if (us_queue_get(queue, (void**)&place, timeout) < 0) {
		return -1;
	}
	return *place;
}

void _release(us_ring_s *ring, us_queue_s *queue, uint index) {
	assert(!us_queue_put(queue, (void*)(ring->places + index), 0));
}
