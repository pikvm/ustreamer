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
	sig_atomic_t		*volatile dev_stop;
	bool				*workers_stop;

	pthread_mutex_t		*last_comp_time_mutex;
	long double			*last_comp_time;

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

	pthread_mutex_t			last_comp_time_mutex;
	long double				last_comp_time;

	pthread_mutex_t			has_job_mutex;
	bool					has_job;
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

struct captured_picture_t {
	struct picture_t	picture;
	unsigned			width;
	unsigned			height;
	bool				updated;
	pthread_mutex_t		mutex;
};


struct captured_picture_t *captured_picture_init();
void captured_picture_destroy(struct captured_picture_t *captured);

void capture_loop(struct device_t *dev, struct captured_picture_t *capture);
void capture_loop_break(struct device_t *dev);
