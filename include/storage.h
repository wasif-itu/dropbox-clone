
#ifndef STORAGE_H
#define STORAGE_H
#include <stddef.h>

int storage_init(void);
int storage_ensure_userdir(const char *username);

/* write a blob to user's filename (atomic via temp+rename) */
int storage_write_blob(const char *username, const char *filename, const char *buf, size_t n);

/* read whole file into malloc'd buffer; returns NULL on error; len set */
char *storage_read_file(const char *username, const char *filename, size_t *len);

/* delete file */
int storage_delete_file(const char *username, const char *filename);

/* list files for user: returns malloc'd newline-separated string */
char *storage_list_files(const char *username);

#endif /* STORAGE_H */
