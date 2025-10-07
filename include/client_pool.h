#ifndef CLIENT_POOL_H
#define CLIENT_POOL_H

#include "queue.h"

/* start/stop client pool */
int client_pool_start(size_t num_threads, queue_t *client_queue, queue_t *task_queue);
void client_pool_stop(void);

#endif /* CLIENT_POOL_H */
