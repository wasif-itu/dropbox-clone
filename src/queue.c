#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

queue_t *queue_create(size_t capacity) {
    if (capacity == 0) return NULL;
    queue_t *q = calloc(1, sizeof(queue_t));
    if (!q) return NULL;
    q->buf = calloc(capacity, sizeof(void*));
    if (!q->buf) { free(q); return NULL; }
    q->capacity = capacity;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    q->head = q->tail = q->count = 0;
    q->closed = 0;
    return q;
}

void queue_destroy(queue_t *q) {
    if (!q) return;
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->mtx);
    free(q->buf);
    free(q);
}

int queue_push(queue_t *q, void *item) {
    if (!q) return -1;
    pthread_mutex_lock(&q->mtx);
    while (!q->closed && q->count == q->capacity) {
        pthread_cond_wait(&q->not_full, &q->mtx);
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }
    q->buf[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

void *queue_pop(queue_t *q) {
    if (!q) return NULL;
    pthread_mutex_lock(&q->mtx);
    while (!q->closed && q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }
    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->mtx);
        return NULL;
    }
    void *item = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
    return item;
}

void queue_close(queue_t *q) {
    if (!q) return;
    pthread_mutex_lock(&q->mtx);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
}
