#define _POSIX_C_SOURCE 200809L
#include "worker_pool.h"
#include "queue.h"
#include "server_types.h"
#include "storage.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* worker running state is driven by queue_close; no atomic needed */

/* Simple file-lock map */
typedef struct file_lock_entry {
    char key[512]; /* username/filename */
    pthread_mutex_t mtx;
    struct file_lock_entry *next;
    int ref;
} file_lock_entry;

static file_lock_entry *file_locks = NULL;
static pthread_mutex_t file_locks_mtx = PTHREAD_MUTEX_INITIALIZER;

static file_lock_entry *fl_get_or_create(const char *username, const char *filename) {
    char key[512];
    snprintf(key, sizeof(key), "%s/%s", username, filename);
    pthread_mutex_lock(&file_locks_mtx);
    file_lock_entry *cur = file_locks;
    while (cur) {
        if (strcmp(cur->key, key) == 0) { cur->ref++; pthread_mutex_unlock(&file_locks_mtx); return cur; }
        cur = cur->next;
    }
    file_lock_entry *n = calloc(1, sizeof(file_lock_entry));
    strncpy(n->key, key, sizeof(n->key)-1);
    pthread_mutex_init(&n->mtx, NULL);
    n->ref = 1;
    n->next = file_locks;
    file_locks = n;
    pthread_mutex_unlock(&file_locks_mtx);
    return n;
}

static void fl_release(file_lock_entry *e) {
    pthread_mutex_lock(&file_locks_mtx);
    e->ref--;
    if (e->ref == 0) {
        /* remove from list */
        file_lock_entry **pp = &file_locks;
        while (*pp && *pp != e) pp = &(*pp)->next;
        if (*pp == e) {
            *pp = e->next;
        }
        pthread_mutex_unlock(&file_locks_mtx);
        pthread_mutex_destroy(&e->mtx);
        free(e);
        return;
    }
    pthread_mutex_unlock(&file_locks_mtx);
}

/* worker threads */
static pthread_t *worker_threads = NULL;
static size_t worker_count = 0;
static queue_t *task_queue_global = NULL;
static unsigned long task_id_counter = 1;
static pthread_mutex_t task_id_mtx = PTHREAD_MUTEX_INITIALIZER;

static unsigned long next_task_id(void) {
    pthread_mutex_lock(&task_id_mtx);
    unsigned long v = task_id_counter++;
    pthread_mutex_unlock(&task_id_mtx);
    return v;
}

static void deliver_result_to_session(ClientSession *sess, TaskResult *res) {
    pthread_mutex_lock(&sess->resp_lock);
    /* if session closed, free result */
    if (!sess->alive) {
        pthread_mutex_unlock(&sess->resp_lock);
        if (res->payload) free(res->payload);
        free(res);
        return;
    }
    sess->pending_result = res;
    pthread_cond_signal(&sess->resp_cv);
    pthread_mutex_unlock(&sess->resp_lock);
}

static void worker_do_task(Task *t) {
    TaskResult *res = calloc(1, sizeof(TaskResult));
    res->task_id = t->task_id;
    res->status = -1;
    res->payload = NULL;
    res->payload_size = 0;
    res->errmsg[0] = '\0';

    const char *username = t->session->username[0] ? t->session->username : "default";

    if (t->type == TASK_UPLOAD) {
        /* lock file */
        file_lock_entry *fe = fl_get_or_create(username, t->filename);
        pthread_mutex_lock(&fe->mtx);
    int w = storage_write_blob(username, t->filename, t->upload_data ? t->upload_data : "", t->upload_data ? t->filesize : 0);
        pthread_mutex_unlock(&fe->mtx);
        fl_release(fe);
        if (w == 0) {
            res->status = 0;
        } else {
            res->status = -1;
            snprintf(res->errmsg, sizeof(res->errmsg), "write failed");
        }
    } else if (t->type == TASK_DOWNLOAD) {
        file_lock_entry *fe = fl_get_or_create(username, t->filename);
        pthread_mutex_lock(&fe->mtx);
        size_t len = 0;
        char *buf = storage_read_file(username, t->filename, &len);
        pthread_mutex_unlock(&fe->mtx);
        fl_release(fe);
        if (buf) {
            res->status = 0;
            res->payload = buf;
            res->payload_size = len;
        } else {
            res->status = -1;
            snprintf(res->errmsg, sizeof(res->errmsg), "not found");
        }
    } else if (t->type == TASK_LIST) {
        char *list = storage_list_files(username);
        if (list) {
            res->status = 0;
            res->payload = list;
            res->payload_size = strlen(list);
        } else {
            res->status = -1;
            snprintf(res->errmsg, sizeof(res->errmsg), "list failed");
        }
    } else if (t->type == TASK_DELETE) {
        file_lock_entry *fe = fl_get_or_create(username, t->filename);
        pthread_mutex_lock(&fe->mtx);
        int d = storage_delete_file(username, t->filename);
        pthread_mutex_unlock(&fe->mtx);
        fl_release(fe);
        if (d == 0) res->status = 0;
        else { res->status = -1; snprintf(res->errmsg, sizeof(res->errmsg), "delete failed"); }
    } else {
        res->status = -1;
        snprintf(res->errmsg, sizeof(res->errmsg), "unknown task");
    }

    /* free upload_data (ownership transferred to worker) */
    if (t->upload_data) {
        free(t->upload_data);
        t->upload_data = NULL;
    }

    /* deliver result */
    deliver_result_to_session(t->session, res);

    /* free task */
    free(t);
}

static void *worker_thread_main(void *arg) {
    (void)arg;
    while (1) {
        Task *t = (Task *)queue_pop(task_queue_global);
        if (!t) break;
        worker_do_task(t);
    }
    return NULL;
}

int worker_pool_start(size_t num_threads, queue_t *task_queue) {
    if (worker_threads != NULL) return -1;
    if (!task_queue || num_threads == 0) return -1;
    task_queue_global = task_queue;
    worker_threads = calloc(num_threads, sizeof(pthread_t));
    if (!worker_threads) return -1;
    worker_count = num_threads;
    for (size_t i = 0; i < num_threads; ++i) {
        pthread_create(&worker_threads[i], NULL, worker_thread_main, NULL);
    }
    return 0;
}

void worker_pool_stop(void) {
    if (!worker_threads) return;
    /* close the task queue to wake workers */
    queue_close(task_queue_global);
    for (size_t i = 0; i < worker_count; ++i) {
        pthread_join(worker_threads[i], NULL);
    }
    free(worker_threads);
    worker_threads = NULL;
    worker_count = 0;
    task_queue_global = NULL;
}
