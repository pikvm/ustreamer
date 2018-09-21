/*****************************************************************************
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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

#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

#include "device.h"


struct worker_context_t {
	int					index;
	struct device_t		*dev;
	struct v4l2_buffer	buf_info;
	sig_atomic_t		*volatile dev_stop;
	bool				*workers_stop;

	pthread_mutex_t		*last_comp_time_mutex;
	long double			*last_comp_time;

	pthread_mutex_t		*has_job_mutex;
	bool				*has_job;
	bool				*job_failed;
	pthread_cond_t		*has_job_cond;

	pthread_mutex_t		*has_free_workers_mutex;
	bool				*has_free_workers;
	pthread_cond_t		*has_free_workers_cond;
};

struct worker_t {
	struct worker_context_t	ctx;
	pthread_t				tid;

	pthread_mutex_t			last_comp_time_mutex;
	long double				last_comp_time;

	pthread_mutex_t			has_job_mutex;
	bool					has_job;
	bool					job_failed;
	pthread_cond_t			has_job_cond;

	struct worker_t			*order_next;
};

struct workers_pool_t {
	struct worker_t	*workers;
	bool			*workers_stop;

	pthread_mutex_t	has_free_workers_mutex;
	bool			has_free_workers;
	pthread_cond_t	has_free_workers_cond;
};

struct stream_t {
	struct picture_t	picture;
	unsigned			width;
	unsigned			height;
	bool				updated;
	pthread_mutex_t		mutex;
	struct device_t		*dev;
};


struct stream_t *stream_init(struct device_t *dev);
void stream_destroy(struct stream_t *stream);

void stream_loop(struct stream_t *stream);
void stream_loop_break(struct stream_t *stream);
