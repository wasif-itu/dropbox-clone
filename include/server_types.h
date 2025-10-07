#ifndef SERVER_TYPES_H
#define SERVER_TYPES_H

#include <pthread.h>
#include <stddef.h>

/* Task types */
typedef enum { TASK_UPLOAD, TASK_DOWNLOAD, TASK_DELETE, TASK_LIST } TaskType;

typedef struct ClientSession {
    int sockfd;
    char username[64];   /* set after login */
    int logged_in;
    pthread_mutex_t resp_lock;
    pthread_cond_t resp_cv;
    struct TaskResult *pending_result; /* worker writes here & signals */
    int alive; /* 1 while session active */
} ClientSession;

typedef struct Task {
    TaskType type;
    char filename[256];
    size_t filesize;        /* for upload if client provides size */
    char *upload_data;      /* allocated by client thread, freed by worker */
    ClientSession *session; /* pointer to originating client session */
    unsigned long task_id;
} Task;

typedef struct TaskResult {
    int status;            /* 0 OK, -1 error */
    char *payload;         /* for DOWNLOAD or LIST; malloc'd by worker */
    size_t payload_size;
    char errmsg[256];
    unsigned long task_id;
} TaskResult;

#endif /* SERVER_TYPES_H */
