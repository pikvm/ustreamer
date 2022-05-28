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

#include <pthread.h>

#include "tools.h"
#include "threading.h"


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


queue_s *queue_init(unsigned capacity);
void queue_destroy(queue_s *queue);

int queue_put(queue_s *queue, void *item, unsigned timeout);
int queue_get(queue_s *queue, void **item, unsigned timeout);
int queue_get_free(queue_s *queue);
