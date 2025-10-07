#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *ROOT = "server_storage";

int storage_init(void) {
    mkdir(ROOT, 0777);
    return 0;
}

int storage_ensure_userdir(const char *username) {
    if (!username) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", ROOT, username);
    mkdir(path, 0777);
    return 0;
}

int storage_write_blob(const char *username, const char *filename, const char *buf, size_t n) {
    if (!username || !filename) return -1;
    storage_ensure_userdir(username);
    // Strip directory from filename
    const char *base = strrchr(filename, '/');
    if (base) base++;
    else base = filename;
    char path[512], tmp[512];
    snprintf(path, sizeof(path), "%s/%s/%s", ROOT, username, base);
    snprintf(tmp, sizeof(tmp), "%s/%s.tmp", ROOT, username, base);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return -1;
    size_t w = fwrite(buf, 1, n, fp);
    fclose(fp);
    if (w != n) { remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

char *storage_read_file(const char *username, const char *filename, size_t *len) {
    if (!username || !filename) return NULL;
    // Strip directory from filename
    const char *base = strrchr(filename, '/');
    if (base) base++;
    else base = filename;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%s", ROOT, username, base);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t r = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (r != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    if (len) *len = (size_t)sz;
    return buf;
}

int storage_delete_file(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    // Strip directory from filename
    const char *base = strrchr(filename, '/');
    if (base) base++;
    else base = filename;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%s", ROOT, username, base);
    if (unlink(path) == 0) return 0;
    return -1;
}

char *storage_list_files(const char *username) {
    if (!username) return NULL;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", ROOT, username);
    DIR *d = opendir(path);
    if (!d) return NULL;
    size_t cap = 4096;
    char *out = malloc(cap);
    if (!out) { closedir(d); return NULL; }
    out[0] = '\0';
    size_t len = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(fpath, &st) == 0) {
            char line[512];
            int n = snprintf(line, sizeof(line), "%s %lld\n", e->d_name, (long long)st.st_size);
            if (len + (size_t)n + 1 > cap) {
                cap *= 2;
                char *tmp = realloc(out, cap);
                if (!tmp) { free(out); closedir(d); return NULL; }
                out = tmp;
            }
            memcpy(out + len, line, (size_t)n);
            len += (size_t)n;
            out[len] = '\0';
        }
    }
    closedir(d);
    return out;
}
