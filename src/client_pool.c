
#define _POSIX_C_SOURCE 200809L
#include "client_pool.h"
#include "server_types.h"
#include "queue.h"
#include "auth.h"
#include "storage.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 4096
#endif

static pthread_t *client_threads = NULL;
static size_t client_thread_count = 0;
static queue_t *client_queue_global = NULL;
static queue_t *task_queue_global = NULL;
static int client_running = 0;

static ssize_t robust_readline(int fd, char *buf, size_t maxlen) {
    size_t idx = 0;
    while (idx + 1 < maxlen) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) {
            if (idx == 0) return 0;
            break;
        } else if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        } else {
            buf[idx++] = c;
            if (c == '\n') break;
        }
    }
    buf[idx] = '\0';
    return (ssize_t)idx;
}

static ssize_t read_n_bytes(int fd, void *buf, size_t n) {
    size_t left = n;
    char *p = buf;
    while (left) {
        ssize_t r = recv(fd, p, left, 0);
        if (r == 0) return (ssize_t)(n - left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        left -= (size_t)r;
        p += r;
    }
    return (ssize_t)n;
}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t left = len;
    while (left) {
        ssize_t w = send(fd, p, left, 0);
        if (w <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        left -= (size_t)w;
        p += w;
    }
    return 0;
}

static void cleanup_session(ClientSession *sess) {
    if (!sess) return;
    pthread_mutex_lock(&sess->resp_lock);
    sess->alive = 0;
    pthread_cond_broadcast(&sess->resp_cv);
    pthread_mutex_unlock(&sess->resp_lock);
    pthread_mutex_destroy(&sess->resp_lock);
    pthread_cond_destroy(&sess->resp_cv);
    close(sess->sockfd);
    free(sess);
}

static void client_handle_connection(int client_fd) {
    /* allocate session */
    ClientSession *sess = calloc(1, sizeof(ClientSession));
    sess->sockfd = client_fd;
    sess->username[0] = '\0';
    sess->logged_in = 0;
    pthread_mutex_init(&sess->resp_lock, NULL);
    pthread_cond_init(&sess->resp_cv, NULL);
    sess->pending_result = NULL;
    sess->alive = 1;

    char line[BUFFER_SIZE];

    /* Authentication loop: require SIGNUP or LOGIN */
    while (1) {
        ssize_t n = robust_readline(client_fd, line, sizeof(line));
        if (n <= 0) { cleanup_session(sess); return; }
        /* strip newline */
        if (line[n-1] == '\n') line[n-1] = '\0';
        char cmd[16], user[64], pass[64];
        if (sscanf(line, "%15s %63s %63s", cmd, user, pass) >= 1) {
            if (strcmp(cmd, "SIGNUP") == 0) {
                if (auth_signup(user, pass) == 0) {
                    storage_ensure_userdir(user);
                    send_all(client_fd, "OK signup\n", strlen("OK signup\n"));
                    /* keep looping to allow immediate LOGIN */
                } else {
                    send_all(client_fd, "ERR userexists\n", strlen("ERR userexists\n"));
                }
            } else if (strcmp(cmd, "LOGIN") == 0) {
                if (auth_login(user, pass) == 0) {
                    strncpy(sess->username, user, sizeof(sess->username)-1);
                    sess->logged_in = 1;
                    send_all(client_fd, "OK login\n", strlen("OK login\n"));
                    break;
                } else {
                    send_all(client_fd, "ERR badcreds\n", strlen("ERR badcreds\n"));
                }
            } else {
                send_all(client_fd, "ERR need SIGNUP/LOGIN\n", strlen("ERR need SIGNUP/LOGIN\n"));
            }
        } else {
            send_all(client_fd, "ERR invalid\n", strlen("ERR invalid\n"));
        }
    }

    /* Command loop (after login) */
    while (sess->alive) {
        ssize_t n = robust_readline(client_fd, line, sizeof(line));
        if (n <= 0) break;
        if (line[n-1] == '\n') line[n-1] = '\0';
        char cmd[16], fname[256];
        size_t filesize = 0;
        int args = sscanf(line, "%15s %255s %zu", cmd, fname, &filesize);
        if (args >= 1) {
            if (strcmp(cmd, "UPLOAD") == 0 && args >= 3) {
                /* read exact filesize into memory then create task */
                char *buf = malloc(filesize + 1);
                if (!buf) {
                    send_all(client_fd, "ERR nomem\n", strlen("ERR nomem\n"));
                    continue;
                }
                /* tell client ready */
                send_all(client_fd, "READY\n", strlen("READY\n"));
                ssize_t r = read_n_bytes(client_fd, buf, filesize);
                if (r != (ssize_t)filesize) {
                    free(buf);
                    send_all(client_fd, "ERR readfail\n", strlen("ERR readfail\n"));
                    continue;
                }
                buf[filesize] = '\0';
                Task *t = calloc(1, sizeof(Task));
                t->type = TASK_UPLOAD;
                strncpy(t->filename, fname, sizeof(t->filename)-1);
                t->filesize = filesize;
                t->upload_data = buf;
                t->session = sess;
                t->task_id = 0; /* worker assigns id */
                /* push to task queue */
                if (queue_push(task_queue_global, t) != 0) {
                    free(t->upload_data);
                    free(t);
                    send_all(client_fd, "ERR serverbusy\n", strlen("ERR serverbusy\n"));
                    continue;
                }
                /* wait for result */
                pthread_mutex_lock(&sess->resp_lock);
                while (sess->pending_result == NULL && sess->alive) {
                    pthread_cond_wait(&sess->resp_cv, &sess->resp_lock);
                }
                TaskResult *res = sess->pending_result;
                sess->pending_result = NULL;
                pthread_mutex_unlock(&sess->resp_lock);
                if (!res) {
                    send_all(client_fd, "ERR sessionclosed\n", strlen("ERR sessionclosed\n"));
                    continue;
                }
                if (res->status == 0) {
                    send_all(client_fd, "OK upload\n", strlen("OK upload\n"));
                } else {
                    char tmp[256];
                    snprintf(tmp, sizeof(tmp), "ERR upload %s\n", res->errmsg);
                    send_all(client_fd, tmp, strlen(tmp));
                }
                if (res->payload) free(res->payload);
                free(res);
            } else if (strcmp(cmd, "DOWNLOAD") == 0 && args >= 2) {
                Task *t = calloc(1, sizeof(Task));
                t->type = TASK_DOWNLOAD;
                strncpy(t->filename, fname, sizeof(t->filename)-1);
                t->session = sess;
                t->task_id = 0;
                if (queue_push(task_queue_global, t) != 0) {
                    free(t);
                    send_all(client_fd, "ERR serverbusy\n", strlen("ERR serverbusy\n"));
                    continue;
                }
                pthread_mutex_lock(&sess->resp_lock);
                while (sess->pending_result == NULL && sess->alive) {
                    pthread_cond_wait(&sess->resp_cv, &sess->resp_lock);
                }
                TaskResult *res = sess->pending_result;
                sess->pending_result = NULL;
                pthread_mutex_unlock(&sess->resp_lock);
                if (!res) { send_all(client_fd, "ERR sessionclosed\n", strlen("ERR sessionclosed\n")); continue; }
                if (res->status == 0 && res->payload) {
                    /* send OK size\n then raw bytes */
                    char header[128];
                    snprintf(header, sizeof(header), "OK download %zu\n", res->payload_size);
                    send_all(client_fd, header, strlen(header));
                    send_all(client_fd, res->payload, res->payload_size);
                } else {
                    char tmp[256];
                    snprintf(tmp, sizeof(tmp), "ERR download %s\n", res->errmsg);
                    send_all(client_fd, tmp, strlen(tmp));
                }
                if (res->payload) free(res->payload);
                free(res);
            } else if (strcmp(cmd, "LIST") == 0) {
                Task *t = calloc(1, sizeof(Task));
                t->type = TASK_LIST;
                t->session = sess;
                t->task_id = 0;
                if (queue_push(task_queue_global, t) != 0) {
                    free(t);
                    send_all(client_fd, "ERR serverbusy\n", strlen("ERR serverbusy\n"));
                    continue;
                }
                pthread_mutex_lock(&sess->resp_lock);
                while (sess->pending_result == NULL && sess->alive) {
                    pthread_cond_wait(&sess->resp_cv, &sess->resp_lock);
                }
                TaskResult *res = sess->pending_result;
                sess->pending_result = NULL;
                pthread_mutex_unlock(&sess->resp_lock);
                if (!res) { send_all(client_fd, "ERR sessionclosed\n", strlen("ERR sessionclosed\n")); continue; }
                if (res->status == 0 && res->payload) {
                    char header[64];
                    snprintf(header, sizeof(header), "OK list %zu\n", res->payload_size);
                    send_all(client_fd, header, strlen(header));
                    send_all(client_fd, res->payload, res->payload_size);
                } else {
                    char tmp[256];
                    snprintf(tmp, sizeof(tmp), "ERR list %s\n", res->errmsg);
                    send_all(client_fd, tmp, strlen(tmp));
                }
                if (res->payload) free(res->payload);
                free(res);
            } else if (strcmp(cmd, "DELETE") == 0 && args >= 2) {
                Task *t = calloc(1, sizeof(Task));
                t->type = TASK_DELETE;
                strncpy(t->filename, fname, sizeof(t->filename)-1);
                t->session = sess;
                t->task_id = 0;
                if (queue_push(task_queue_global, t) != 0) {
                    free(t);
                    send_all(client_fd, "ERR serverbusy\n", strlen("ERR serverbusy\n"));
                    continue;
                }
                pthread_mutex_lock(&sess->resp_lock);
                while (sess->pending_result == NULL && sess->alive) {
                    pthread_cond_wait(&sess->resp_cv, &sess->resp_lock);
                }
                TaskResult *res = sess->pending_result;
                sess->pending_result = NULL;
                pthread_mutex_unlock(&sess->resp_lock);
                if (!res) { send_all(client_fd, "ERR sessionclosed\n", strlen("ERR sessionclosed\n")); continue; }
                if (res->status == 0) {
                    send_all(client_fd, "OK delete\n", strlen("OK delete\n"));
                } else {
                    char tmp[256];
                    snprintf(tmp, sizeof(tmp), "ERR delete %s\n", res->errmsg);
                    send_all(client_fd, tmp, strlen(tmp));
                }
                if (res->payload) free(res->payload);
                free(res);
            } else if (strcmp(cmd, "QUIT") == 0) {
                send_all(client_fd, "OK bye\n", strlen("OK bye\n"));
                break;
            } else {
                send_all(client_fd, "ERR unknown_command\n", strlen("ERR unknown_command\n"));
            }
        }
    }

    cleanup_session(sess);
}

/* thread main */
static void *client_thread_main(void *arg) {
    (void)arg;
    while (client_running) {
        int *pfd = (int *)queue_pop(client_queue_global);
        if (!pfd) break;
        int client_fd = *pfd;
        free(pfd);
        client_handle_connection(client_fd);
    }
    return NULL;
}

int client_pool_start(size_t num_threads, queue_t *client_queue, queue_t *task_queue) {
    if (client_running) return -1;
    if (!client_queue || !task_queue || num_threads == 0) return -1;
    client_queue_global = client_queue;
    task_queue_global = task_queue;
    client_threads = calloc(num_threads, sizeof(pthread_t));
    if (!client_threads) return -1;
    client_thread_count = num_threads;
    client_running = 1;
    for (size_t i = 0; i < num_threads; ++i) {
        pthread_create(&client_threads[i], NULL, client_thread_main, NULL);
    }
    return 0;
}

void client_pool_stop(void) {
    if (!client_running) return;
    client_running = 0;
    queue_close(client_queue_global);
    for (size_t i = 0; i < client_thread_count; ++i) {
        pthread_join(client_threads[i], NULL);
    }
    free(client_threads);
    client_threads = NULL;
    client_thread_count = 0;
    client_queue_global = NULL;
    task_queue_global = NULL;
}
