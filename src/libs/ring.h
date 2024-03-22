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


#pragma once


#include "types.h"
#include "queue.h"


typedef struct {
	uz			capacity;
	void		**items;
	uint		*places;
	us_queue_s	*producer;
	us_queue_s	*consumer;
} us_ring_s;


#define US_RING_INIT_WITH_ITEMS(x_ring, x_capacity, x_init_item) { \
		(x_ring) = us_ring_init(x_capacity); \
		for (uz m_index = 0; m_index < (x_ring)->capacity; ++m_index) { \
			(x_ring)->items[m_index] = x_init_item(); \
		} \
	}

#define US_RING_DELETE_WITH_ITEMS(x_ring, x_destroy_item) { \
		if (x_ring) { \
			for (uz m_index = 0; m_index < (x_ring)->capacity; ++m_index) { \
				x_destroy_item((x_ring)->items[m_index]); \
			} \
			us_ring_destroy(x_ring); \
		} \
	}


us_ring_s *us_ring_init(uint capacity);
void us_ring_destroy(us_ring_s *ring);

int us_ring_producer_acquire(us_ring_s *ring, ldf timeout);
void us_ring_producer_release(us_ring_s *ring, uint index);

int us_ring_consumer_acquire(us_ring_s *ring, ldf timeout);
void us_ring_consumer_release(us_ring_s *ring, uint index);
