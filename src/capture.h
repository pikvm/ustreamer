#pragma once

#include <signal.h>
#include <pthread.h>

#include "device.h"


struct worker_params {
	int					index;
	struct workers_pool	*pool;
	struct device		*dev;
	sig_atomic_t		*volatile stop;
};

struct worker {
	pthread_t				tid;
	pthread_mutex_t			busy_mutex;
	pthread_cond_t			busy_cond;
	struct worker_params	params;
};

struct workers_pool {
	struct worker	*workers;
	pthread_mutex_t	busy_mutex;
	pthread_cond_t	busy_cond;
};


void capture_loop(struct device *dev, sig_atomic_t *volatile stop);
