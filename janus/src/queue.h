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


#pragma once

#include <errno.h>
#include <time.h>
#include <assert.h>

#include <pthread.h>

#include "uslibs/tools.h"
#include "uslibs/threading.h"


// Based on https://github.com/seifzadeh/c-pthread-queue/blob/master/queue.h

typedef struct {
	void			**items;
	unsigned		size;
	unsigned		capacity;
	unsigned		in;
	unsigned		out;

	pthread_mutex_t	mutex;
	pthread_cond_t	full_cond;
	pthread_cond_t	empty_cond;
} queue_s;


#define QUEUE_FREE_ITEMS_AND_DESTROY(_queue, _free_item) { \
		while (!queue_get_free(_queue)) { \
			void *_ptr; \
			assert(!queue_get(_queue, &_ptr, 0.1)); \
			_free_item(_ptr); \
		} \
		queue_destroy(_queue); \
	}


queue_s *queue_init(unsigned capacity);
void queue_destroy(queue_s *queue);

int queue_put(queue_s *queue, void *item, long double timeout);
int queue_get(queue_s *queue, void **item, long double timeout);
int queue_get_free(queue_s *queue);
