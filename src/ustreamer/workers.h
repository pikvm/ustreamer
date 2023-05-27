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

#include <stdbool.h>
#include <stdatomic.h>

#include <sys/types.h>

#include <pthread.h>

#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"


typedef struct us_worker_sx {
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

	struct us_worker_sx		*prev_wr;
	struct us_worker_sx		*next_wr;

	struct us_workers_pool_sx	*pool;
} us_worker_s;

typedef void *(*us_workers_pool_job_init_f)(void *arg);
typedef void (*us_workers_pool_job_destroy_f)(void *job);
typedef bool (*us_workers_pool_run_job_f)(us_worker_s *wr);

typedef struct us_workers_pool_sx {
	const char		*name;
	long double		desired_interval;

	us_workers_pool_job_destroy_f	job_destroy;
	us_workers_pool_run_job_f		run_job;

	unsigned		n_workers;
	us_worker_s		*workers;
	us_worker_s		*oldest_wr;
	us_worker_s		*latest_wr;

	long double		approx_job_time;

	pthread_mutex_t	free_workers_mutex;
	unsigned		free_workers;
	pthread_cond_t	free_workers_cond;

	atomic_bool		stop;
} us_workers_pool_s;


us_workers_pool_s *us_workers_pool_init(
	const char *name, const char *wr_prefix, unsigned n_workers, long double desired_interval,
	us_workers_pool_job_init_f job_init, void *job_init_arg,
	us_workers_pool_job_destroy_f job_destroy,
	us_workers_pool_run_job_f run_job);

void us_workers_pool_destroy(us_workers_pool_s *pool);

us_worker_s *us_workers_pool_wait(us_workers_pool_s *pool);
void us_workers_pool_assign(us_workers_pool_s *pool, us_worker_s *ready_wr/*, void *job*/);

long double us_workers_pool_get_fluency_delay(us_workers_pool_s *pool, us_worker_s *ready_wr);
