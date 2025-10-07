#ifndef WORKER_POOL_H
#define WORKER_POOL_H

#include "queue.h"

int worker_pool_start(size_t num_threads, queue_t *task_queue);
void worker_pool_stop(void);

#endif /* WORKER_POOL_H */
