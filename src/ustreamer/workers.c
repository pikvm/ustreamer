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


#include "workers.h"

#include <stdatomic.h>

#include <pthread.h>

#include "../libs/types.h"
#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"
#include "../libs/list.h"


static void *_worker_thread(void *v_worker);


us_workers_pool_s *us_workers_pool_init(
	const char *name, const char *wr_prefix, uint n_workers, ldf desired_interval,
	us_workers_pool_job_init_f job_init, void *job_init_arg,
	us_workers_pool_job_destroy_f job_destroy,
	us_workers_pool_run_job_f run_job) {

	US_LOG_INFO("Creating pool %s with %u workers ...", name, n_workers);

	us_workers_pool_s *pool;
	US_CALLOC(pool, 1);
	pool->name = name;
	pool->desired_interval = desired_interval;
	pool->job_destroy = job_destroy;
	pool->run_job = run_job;

	atomic_init(&pool->stop, false);

	pool->n_workers = n_workers;

	US_MUTEX_INIT(pool->free_workers_mutex);
	US_COND_INIT(pool->free_workers_cond);

	for (uint index = 0; index < pool->n_workers; ++index) {
		us_worker_s *wr;
		US_CALLOC(wr, 1);

		wr->number = index;
		US_ASPRINTF(wr->name, "%s-%u", wr_prefix, index);

		US_MUTEX_INIT(wr->has_job_mutex);
		atomic_init(&wr->has_job, false);
		US_COND_INIT(wr->has_job_cond);

		wr->pool = pool;
		wr->job = job_init(job_init_arg);

		US_THREAD_CREATE(wr->tid, _worker_thread, (void*)wr);
		pool->free_workers += 1;

		US_LIST_APPEND(pool->workers, wr);
	}
	return pool;
}

void us_workers_pool_destroy(us_workers_pool_s *pool) {
	US_LOG_INFO("Destroying workers pool %s ...", pool->name);

	atomic_store(&pool->stop, true);
	US_LIST_ITERATE(pool->workers, wr, { // cppcheck-suppress constStatement
		US_MUTEX_LOCK(wr->has_job_mutex);
		atomic_store(&wr->has_job, true); // Final job: die
		US_MUTEX_UNLOCK(wr->has_job_mutex);
		US_COND_SIGNAL(wr->has_job_cond);

		US_THREAD_JOIN(wr->tid);
		US_MUTEX_DESTROY(wr->has_job_mutex);
		US_COND_DESTROY(wr->has_job_cond);

		pool->job_destroy(wr->job);

		free(wr->name);
		free(wr);
	});

	US_MUTEX_DESTROY(pool->free_workers_mutex);
	US_COND_DESTROY(pool->free_workers_cond);

	free(pool);
}

us_worker_s *us_workers_pool_wait(us_workers_pool_s *pool) {
	US_MUTEX_LOCK(pool->free_workers_mutex);
	US_COND_WAIT_FOR(pool->free_workers, pool->free_workers_cond, pool->free_workers_mutex);
	US_MUTEX_UNLOCK(pool->free_workers_mutex);

	us_worker_s *found = NULL;
	US_LIST_ITERATE(pool->workers, wr, { // cppcheck-suppress constStatement
		if (!atomic_load(&wr->has_job) && (found == NULL || found->job_start_ts <= wr->job_start_ts)) {
			found = wr;
		}
	});
	assert(found != NULL);
	US_LIST_REMOVE(pool->workers, found);
	US_LIST_APPEND(pool->workers, found); // Перемещаем в конец списка

	found->job_timely = (found->job_start_ts > pool->job_timely_ts);
	if (found->job_timely) {
		pool->job_timely_ts = found->job_start_ts;
	}
	return found;
}

void us_workers_pool_assign(us_workers_pool_s *pool, us_worker_s *wr) {
	US_MUTEX_LOCK(wr->has_job_mutex);
	atomic_store(&wr->has_job, true);
	US_MUTEX_UNLOCK(wr->has_job_mutex);
	US_COND_SIGNAL(wr->has_job_cond);

	US_MUTEX_LOCK(pool->free_workers_mutex);
	pool->free_workers -= 1;
	US_MUTEX_UNLOCK(pool->free_workers_mutex);
}

ldf us_workers_pool_get_fluency_delay(us_workers_pool_s *pool, const us_worker_s *wr) {
	const ldf approx_job_time = pool->approx_job_time * 0.9 + wr->last_job_time * 0.1;

	US_LOG_VERBOSE("Correcting pool's %s approx_job_time: %.3Lf -> %.3Lf (last_job_time=%.3Lf)",
		pool->name, pool->approx_job_time, approx_job_time, wr->last_job_time);

	pool->approx_job_time = approx_job_time;

	const ldf min_delay = pool->approx_job_time / pool->n_workers; // Среднее время работы размазывается на N воркеров

	if (pool->desired_interval > 0 && min_delay > 0 && pool->desired_interval > min_delay) {
		// Искусственное время задержки на основе желаемого FPS, если включен --desired-fps
		// и аппаратный fps не попадает точно в желаемое значение
		return pool->desired_interval;
	}
	return min_delay;
}

static void *_worker_thread(void *v_worker) {
	us_worker_s *const wr = v_worker;

	US_THREAD_SETTLE("%s", wr->name);
	US_LOG_DEBUG("Hello! I am a worker %s ^_^", wr->name);

	while (!atomic_load(&wr->pool->stop)) {
		US_LOG_DEBUG("Worker %s waiting for a new job ...", wr->name);

		US_MUTEX_LOCK(wr->has_job_mutex);
		US_COND_WAIT_FOR(atomic_load(&wr->has_job), wr->has_job_cond, wr->has_job_mutex);
		US_MUTEX_UNLOCK(wr->has_job_mutex);

		if (!atomic_load(&wr->pool->stop)) {
			const ldf job_start_ts = us_get_now_monotonic();
			wr->job_failed = !wr->pool->run_job(wr);
			if (!wr->job_failed) {
				wr->job_start_ts = job_start_ts;
				wr->last_job_time = us_get_now_monotonic() - wr->job_start_ts;
			}
			atomic_store(&wr->has_job, false);
		}

		US_MUTEX_LOCK(wr->pool->free_workers_mutex);
		wr->pool->free_workers += 1;
		US_MUTEX_UNLOCK(wr->pool->free_workers_mutex);
		US_COND_SIGNAL(wr->pool->free_workers_cond);
	}

	US_LOG_DEBUG("Bye-bye (worker %s)", wr->name);
	return NULL;
}
