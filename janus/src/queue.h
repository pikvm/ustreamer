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
} us_queue_s;


#define US_QUEUE_DELETE_WITH_ITEMS(x_queue, x_free_item) { \
		if (x_queue) { \
			while (!us_queue_get_free(x_queue)) { \
				void *m_ptr; \
				if (!us_queue_get(x_queue, &m_ptr, 0)) { \
					US_DELETE(m_ptr, x_free_item); \
				} \
			} \
			us_queue_destroy(x_queue); \
		} \
	}


us_queue_s *us_queue_init(unsigned capacity);
void us_queue_destroy(us_queue_s *queue);

int us_queue_put(us_queue_s *queue, void *item, long double timeout);
int us_queue_get(us_queue_s *queue, void **item, long double timeout);
int us_queue_get_free(us_queue_s *queue);
