#ifndef DROPBOX_H
#define DROPBOX_H

#define SERVER_PORT 8080
#define BUFFER_SIZE 2048

/* queue capacities */
#define CLIENT_QUEUE_CAP 256
#define TASK_QUEUE_CAP 1024

/* threadpool sizes (tune as needed) */
#define CLIENT_POOL_SIZE 4
#define WORKER_POOL_SIZE 4

#endif /* DROPBOX_H */
