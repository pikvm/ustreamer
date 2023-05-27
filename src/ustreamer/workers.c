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


#include "workers.h"


static void *_worker_thread(void *v_worker);


us_workers_pool_s *us_workers_pool_init(
	const char *name, const char *wr_prefix, unsigned n_workers, long double desired_interval,
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
	US_CALLOC(pool->workers, pool->n_workers);

	US_MUTEX_INIT(pool->free_workers_mutex);
	US_COND_INIT(pool->free_workers_cond);

	for (unsigned number = 0; number < pool->n_workers; ++number) {
#		define WR(x_next) pool->workers[number].x_next

		WR(number) = number;
		US_ASPRINTF(WR(name), "%s-%u", wr_prefix, number);

		US_MUTEX_INIT(WR(has_job_mutex));
		atomic_init(&WR(has_job), false);
		US_COND_INIT(WR(has_job_cond));

		WR(pool) = pool;
		WR(job) = job_init(job_init_arg);

		US_THREAD_CREATE(WR(tid), _worker_thread, (void *)&(pool->workers[number]));
		pool->free_workers += 1;

#		undef WR
	}
	return pool;
}

void us_workers_pool_destroy(us_workers_pool_s *pool) {
	US_LOG_INFO("Destroying workers pool %s ...", pool->name);

	atomic_store(&pool->stop, true);
	for (unsigned number = 0; number < pool->n_workers; ++number) {
#		define WR(x_next) pool->workers[number].x_next

		US_MUTEX_LOCK(WR(has_job_mutex));
		atomic_store(&WR(has_job), true); // Final job: die
		US_MUTEX_UNLOCK(WR(has_job_mutex));
		US_COND_SIGNAL(WR(has_job_cond));

		US_THREAD_JOIN(WR(tid));
		US_MUTEX_DESTROY(WR(has_job_mutex));
		US_COND_DESTROY(WR(has_job_cond));

		free(WR(name));

		pool->job_destroy(WR(job));

#		undef WR
	}

	US_MUTEX_DESTROY(pool->free_workers_mutex);
	US_COND_DESTROY(pool->free_workers_cond);

	free(pool->workers);
	free(pool);
}

us_worker_s *us_workers_pool_wait(us_workers_pool_s *pool) {
	us_worker_s *ready_wr = NULL;

	US_MUTEX_LOCK(pool->free_workers_mutex);
	US_COND_WAIT_FOR(pool->free_workers, pool->free_workers_cond, pool->free_workers_mutex);
	US_MUTEX_UNLOCK(pool->free_workers_mutex);

	if (pool->oldest_wr && !atomic_load(&pool->oldest_wr->has_job)) {
		ready_wr = pool->oldest_wr;
		ready_wr->job_timely = true;
		pool->oldest_wr = pool->oldest_wr->next_wr;
	} else {
		for (unsigned number = 0; number < pool->n_workers; ++number) {
			if (
				!atomic_load(&pool->workers[number].has_job) && (
					ready_wr == NULL
					|| ready_wr->job_start_ts < pool->workers[number].job_start_ts
				)
			) {
				ready_wr = &pool->workers[number];
				break;
			}
		}
		assert(ready_wr != NULL);
		ready_wr->job_timely = false; // Освободился воркер, получивший задание позже (или самый первый при самом первом захвате)
	}
	return ready_wr;
}

void us_workers_pool_assign(us_workers_pool_s *pool, us_worker_s *ready_wr/*, void *job*/) {
	if (pool->oldest_wr == NULL) {
		pool->oldest_wr = ready_wr;
		pool->latest_wr = pool->oldest_wr;
	} else {
		if (ready_wr->next_wr != NULL) {
			ready_wr->next_wr->prev_wr = ready_wr->prev_wr;
		}
		if (ready_wr->prev_wr != NULL) {
			ready_wr->prev_wr->next_wr = ready_wr->next_wr;
		}
		ready_wr->prev_wr = pool->latest_wr;
		pool->latest_wr->next_wr = ready_wr;
		pool->latest_wr = ready_wr;
	}
	pool->latest_wr->next_wr = NULL;

	US_MUTEX_LOCK(ready_wr->has_job_mutex);
	//ready_wr->job = job;
	atomic_store(&ready_wr->has_job, true);
	US_MUTEX_UNLOCK(ready_wr->has_job_mutex);
	US_COND_SIGNAL(ready_wr->has_job_cond);

	US_MUTEX_LOCK(pool->free_workers_mutex);
	pool->free_workers -= 1;
	US_MUTEX_UNLOCK(pool->free_workers_mutex);
}

long double us_workers_pool_get_fluency_delay(us_workers_pool_s *pool, us_worker_s *ready_wr) {
	const long double approx_job_time = pool->approx_job_time * 0.9 + ready_wr->last_job_time * 0.1;

	US_LOG_VERBOSE("Correcting pool's %s approx_job_time: %.3Lf -> %.3Lf (last_job_time=%.3Lf)",
		pool->name, pool->approx_job_time, approx_job_time, ready_wr->last_job_time);

	pool->approx_job_time = approx_job_time;

	const long double min_delay = pool->approx_job_time / pool->n_workers; // Среднее время работы размазывается на N воркеров

	if (pool->desired_interval > 0 && min_delay > 0 && pool->desired_interval > min_delay) {
		// Искусственное время задержки на основе желаемого FPS, если включен --desired-fps
		// и аппаратный fps не попадает точно в желаемое значение
		return pool->desired_interval;
	}
	return min_delay;
}

static void *_worker_thread(void *v_worker) {
	us_worker_s *wr = (us_worker_s *)v_worker;

	US_THREAD_RENAME("%s", wr->name);
	US_LOG_DEBUG("Hello! I am a worker %s ^_^", wr->name);

	while (!atomic_load(&wr->pool->stop)) {
		US_LOG_DEBUG("Worker %s waiting for a new job ...", wr->name);

		US_MUTEX_LOCK(wr->has_job_mutex);
		US_COND_WAIT_FOR(atomic_load(&wr->has_job), wr->has_job_cond, wr->has_job_mutex);
		US_MUTEX_UNLOCK(wr->has_job_mutex);

		if (!atomic_load(&wr->pool->stop)) {
			const long double job_start_ts = us_get_now_monotonic();
			wr->job_failed = !wr->pool->run_job(wr);
			if (!wr->job_failed) {
				wr->job_start_ts = job_start_ts;
				wr->last_job_time = us_get_now_monotonic() - wr->job_start_ts;
			}
			//wr->job = NULL;
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
