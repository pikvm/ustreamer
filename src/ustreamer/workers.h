/*****************************************************************************
#                                                                            #
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
#include <stdatomic.h>

#include <sys/types.h>

#include <pthread.h>

#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"


typedef struct worker_sx {
	pthread_t		tid;
	unsigned		number;
	char			*name;

	long double		last_job_time;

	pthread_mutex_t	has_job_mutex;
	void			*job;
	atomic_bool		has_job;
	bool			job_timely;
	bool			job_failed;
	long double		job_start_ts;
	pthread_cond_t	has_job_cond;

	struct worker_sx *prev_wr;
	struct worker_sx *next_wr;

	struct workers_pool_sx *pool;
} worker_s;

typedef struct workers_pool_sx {
	const char		*name;
	long double		desired_interval;

	bool (*run_job)(worker_s *wr);
	void (*job_destroy)(void *job);

	unsigned		n_workers;
	worker_s		*workers;
	worker_s		*oldest_wr;
	worker_s		*latest_wr;

	long double		approx_job_time;

	pthread_mutex_t	free_workers_mutex;
	unsigned		free_workers;
	pthread_cond_t	free_workers_cond;

	atomic_bool		stop;
} workers_pool_s;


workers_pool_s *workers_pool_init(
	const char *name, unsigned n_workers, long double desired_interval,
	void *(*job_init)(worker_s *wr, void *arg), void *job_init_arg,
	void (*job_destroy)(void *job),
	bool (*run_job)(worker_s *));

void workers_pool_destroy(workers_pool_s *pool);

worker_s *workers_pool_wait(workers_pool_s *pool);
void workers_pool_assign(workers_pool_s *pool, worker_s *ready_wr/*, void *job*/);

long double workers_pool_get_fluency_delay(workers_pool_s *pool, worker_s *ready_wr);
