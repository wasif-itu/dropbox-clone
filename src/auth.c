
#define _POSIX_C_SOURCE 200809L
#include "auth.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

typedef struct user_entry {
    char username[64];
    char password[64];
    struct user_entry *next;
} user_entry;


static user_entry *users = NULL;
static pthread_mutex_t users_mtx = PTHREAD_MUTEX_INITIALIZER;
static const char *USER_FILE = "server_storage/users.txt";

static void auth_load_users(void) {
    FILE *fp = fopen(USER_FILE, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char username[64], password[64];
        if (sscanf(line, "%63s %63s", username, password) == 2) {
            user_entry *n = calloc(1, sizeof(user_entry));
            strncpy(n->username, username, sizeof(n->username)-1);
            strncpy(n->password, password, sizeof(n->password)-1);
            n->next = users;
            users = n;
        }
    }
    fclose(fp);
}

static void auth_save_users(void) {
    FILE *fp = fopen(USER_FILE, "w");
    if (!fp) return;
    user_entry *cur = users;
    while (cur) {
        fprintf(fp, "%s %s\n", cur->username, cur->password);
        cur = cur->next;
    }
    fclose(fp);
}

int auth_init(void) {
    pthread_mutex_lock(&users_mtx);
    auth_load_users();
    pthread_mutex_unlock(&users_mtx);
    return 0;
}

void auth_shutdown(void) {
    pthread_mutex_lock(&users_mtx);
    auth_save_users();
    user_entry *cur = users;
    while (cur) {
        user_entry *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    users = NULL;
    pthread_mutex_unlock(&users_mtx);
}

int auth_signup(const char *username, const char *password) {
    if (!username || !password) return -1;
    pthread_mutex_lock(&users_mtx);
    user_entry *cur = users;
    while (cur) {
        if (strcmp(cur->username, username) == 0) {
            pthread_mutex_unlock(&users_mtx);
            return -1; /* exists */
        }
        cur = cur->next;
    }
    user_entry *n = calloc(1, sizeof(user_entry));
    strncpy(n->username, username, sizeof(n->username)-1);
    strncpy(n->password, password, sizeof(n->password)-1);
    n->next = users;
    users = n;
    auth_save_users();
    pthread_mutex_unlock(&users_mtx);
    return 0;
}

int auth_login(const char *username, const char *password) {
    if (!username || !password) return -1;
    pthread_mutex_lock(&users_mtx);
    user_entry *cur = users;
    while (cur) {
        if (strcmp(cur->username, username) == 0 && strcmp(cur->password, password) == 0) {
            pthread_mutex_unlock(&users_mtx);
            return 0;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&users_mtx);
    return -1;
}
