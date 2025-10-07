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

static queue_t *client_queue = NULL;
static queue_t *task_queue = NULL;
static int server_fd = -1;

static void handle_sigint(int sig) {
    (void)sig;
    printf("\nShutting down server...\n");
    if (server_fd != -1) close(server_fd);
    if (client_queue) queue_close(client_queue);
    if (task_queue) queue_close(task_queue);
    /* client_pool_stop() and worker_pool_stop() will be called after accept loop breaks */
}

int main(void) {
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

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0) {
            /* likely interrupted by signal -> break */
            break;
        }
        /* push fd pointer to client queue */
        int *pfd = malloc(sizeof(int));
        *pfd = client_fd;
        if (queue_push(client_queue, pfd) != 0) {
            /* queue closed or push failed */
            close(client_fd);
            free(pfd);
        }
    }

    /* shutdown sequence */
    client_pool_stop();
    worker_pool_stop();

    queue_destroy(client_queue);
    queue_destroy(task_queue);

    auth_shutdown();

    printf("Server stopped.\n");
    return 0;
}
