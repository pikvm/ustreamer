#pragma once

#include <signal.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <linux/videodev2.h>

#include "device.h"


struct worker_context_t {
	int					index;
	struct device_t		*dev;
	struct v4l2_buffer	buf_info;
	sig_atomic_t		*volatile global_stop;
	sig_atomic_t		*volatile workers_stop;

	pthread_mutex_t		*has_job_mutex;
	bool				*has_job;
	pthread_cond_t		*has_job_cond;

	pthread_mutex_t		*has_free_workers_mutex;
	bool				*has_free_workers;
	pthread_cond_t		*has_free_workers_cond;
};

struct worker_t {
	struct worker_context_t	ctx;
	pthread_t				tid;

	pthread_mutex_t			has_job_mutex;
	bool					has_job;
	pthread_cond_t			has_job_cond;
};

struct workers_pool_t {
	struct worker_t	*workers;
	sig_atomic_t	*volatile workers_stop;

	pthread_mutex_t	has_free_workers_mutex;
	bool			has_free_workers;
	pthread_cond_t	has_free_workers_cond;
};


void capture_loop(struct device_t *dev, sig_atomic_t *volatile global_stop);
