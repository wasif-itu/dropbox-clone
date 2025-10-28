#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>

typedef struct queue_t {
    void **buf;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    atomic_int closed;
} queue_t;

queue_t *queue_create(size_t capacity);
void queue_destroy(queue_t *q);
int queue_push(queue_t *q, void *item); /* returns 0 on success, -1 if closed/error */
void *queue_pop(queue_t *q);             /* returns item or NULL if closed and empty */
void queue_close(queue_t *q);

#endif /* QUEUE_H */
