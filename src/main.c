#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include "client_pool.h"
#include "worker_pool.h"
#include "auth.h"
#include "storage.h"
#include "dropbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

static queue_t *client_queue = NULL;
static queue_t *task_queue = NULL;
static int server_fd = -1;
/* self-pipe fds for safe signal handling */
static int sig_pipe_fds[2] = {-1, -1};

static void handle_sigint(int sig) {
    (void)sig;
    /* async-signal-safe: write a byte to the pipe to notify main loop */
    if (sig_pipe_fds[1] != -1) {
        char c = 'x';
        ssize_t r = write(sig_pipe_fds[1], &c, 1);
        (void)r; /* ignore write errors in handler */
    }
}

int main(void) {
    /* create self-pipe before installing handler */
    if (pipe(sig_pipe_fds) != 0) {
        perror("pipe");
        return 1;
    }
    /* make read end non-blocking (safe) */
    int flags = fcntl(sig_pipe_fds[0], F_GETFL, 0);
    if (flags != -1) fcntl(sig_pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    signal(SIGINT, handle_sigint);

    auth_init();
    storage_init();

    client_queue = queue_create(CLIENT_QUEUE_CAP);
    task_queue = queue_create(TASK_QUEUE_CAP);

    if (!client_queue || !task_queue) {
        fprintf(stderr, "Failed to create queues\n");
        return 1;
    }

    if (client_pool_start(CLIENT_POOL_SIZE, client_queue, task_queue) != 0) {
        fprintf(stderr, "Failed to start client pool\n");
        return 1;
    }

    if (worker_pool_start(WORKER_POOL_SIZE, task_queue) != 0) {
        fprintf(stderr, "Failed to start worker pool\n");
        return 1;
    }

    /* TCP listen */
    int opt = 1;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return 1;
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 64) < 0) {
        perror("listen");
        return 1;
    }

    printf("Server listening on %d\n", SERVER_PORT);

    /* Use poll to wait on listening socket and the signal pipe */
    struct pollfd fds[2];
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;
    fds[1].fd = sig_pipe_fds[0];
    fds[1].events = POLLIN;

    while (1) {
        int rv = poll(fds, 2, -1);
        if (rv < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        /* signal pipe triggered -> shutdown */
        if (fds[1].revents & POLLIN) {
            char buf[32];
            /* drain pipe */
            while (read(sig_pipe_fds[0], buf, sizeof(buf)) > 0) {}
            break;
        }
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (client_fd < 0) continue;
            /* push fd pointer to client queue */
            int *pfd = malloc(sizeof(int));
            *pfd = client_fd;
            if (queue_push(client_queue, pfd) != 0) {
                /* queue closed or push failed */
                close(client_fd);
                free(pfd);
            }
        }
    }

    /* cleanup the signal pipe */
    if (sig_pipe_fds[0] != -1) close(sig_pipe_fds[0]);
    if (sig_pipe_fds[1] != -1) close(sig_pipe_fds[1]);

    /* shutdown sequence: close queues to wake worker threads */
    if (client_queue) queue_close(client_queue);
    if (task_queue) queue_close(task_queue);

    client_pool_stop();
    worker_pool_stop();

    queue_destroy(client_queue);
    queue_destroy(task_queue);

    auth_shutdown();

    printf("Server stopped.\n");
    return 0;
}
