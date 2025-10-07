
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include "dropbox.h"

#ifndef SERVER_PORT
#define SERVER_PORT 8080
#endif

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

int main() {
    char server_ip[64] = "127.0.0.1";
    int port = SERVER_PORT;
    int sock;
    struct sockaddr_in serv_addr;

    printf("Client: connecting to %s:%d\n", server_ip, port);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        return -1;
    }

    printf("Commands available:\n");
    printf("SIGNUP <user> <pass>\n");
    printf("LOGIN <user> <pass>\n");
    printf("UPLOAD <filename>    (will prompt filesize)\n");
    printf("DOWNLOAD <filename>\n");
    printf("LIST\n");
    printf("DELETE <filename>\n");
    printf("QUIT\n");

    char line[1024];
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        /* remove newline */
        size_t L = strlen(line);
        if (L && line[L-1] == '\n') line[L-1] = '\0';
        if (strncmp(line, "UPLOAD ", 7) == 0) {
            char filename[256];
            if (sscanf(line+7, "%255s", filename) != 1) { printf("Usage: UPLOAD <filename>\n"); continue; }
            /* get file size */
            FILE *fp = fopen(filename, "rb");
            if (!fp) { perror("fopen"); continue; }
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (sz < 0) { fclose(fp); printf("Cannot stat file\n"); continue; }
            /* send command with size */
            char header[512];
            snprintf(header, sizeof(header), "UPLOAD %s %ld\n", filename, sz);
            if (send_all(sock, header, strlen(header)) != 0) { perror("send"); fclose(fp); break; }
            /* wait for READY\n */
            char reply[256];
            ssize_t r = recv(sock, reply, sizeof(reply)-1, 0);
            if (r <= 0) { perror("recv"); fclose(fp); break; }
            reply[r] = '\0';
            if (strncmp(reply, "READY", 5) != 0) { printf("Server not ready: %s\n", reply); fclose(fp); continue; }
            /* send file bytes */
            char buf[4096];
            size_t rr;
            while ((rr = fread(buf, 1, sizeof(buf), fp)) > 0) {
                if (send_all(sock, buf, rr) != 0) { perror("send"); break; }
            }
            fclose(fp);
            /* server will later send OK upload or ERR ...; read line */
            char res[256];
            ssize_t s = recv(sock, res, sizeof(res)-1, 0);
            if (s > 0) { res[s] = '\0'; printf("%s", res); }
            else { perror("recv"); break; }
        } else if (strncmp(line, "DOWNLOAD ", 9) == 0) {
            char filename[256];
            if (sscanf(line+9, "%255s", filename) != 1) { printf("Usage: DOWNLOAD <filename>\n"); continue; }
            char header[512];
            snprintf(header, sizeof(header), "DOWNLOAD %s\n", filename);
            if (send_all(sock, header, strlen(header)) != 0) { perror("send"); break; }
            /* read response header or OK line */
            char resp[128];
            ssize_t r = recv(sock, resp, sizeof(resp)-1, 0);
            if (r <= 0) { perror("recv"); break; }
            resp[r] = '\0';
            if (strncmp(resp, "OK download ", 12) == 0) {
                size_t size = 0;
                sscanf(resp + 12, "%zu", &size);
                /* receive exact size */
                char *buf = malloc(size);
                if (!buf) { printf("alloc fail\n"); break; }
                ssize_t got = read_n_bytes(sock, buf, size);
                if (got != (ssize_t)size) { printf("incomplete download\n"); free(buf); break; }
                FILE *fp = fopen(filename, "wb");
                if (!fp) { perror("fopen"); free(buf); continue; }
                fwrite(buf, 1, size, fp);
                fclose(fp);
                free(buf);
                printf("Downloaded %s (%zu bytes)\n", filename, size);
            } else {
                printf("%s", resp);
            }
        } else {
            /* SEND command with newline and print immediate server reply if any */
            char cmdline[1024];
            snprintf(cmdline, sizeof(cmdline), "%s\n", line);
            if (send_all(sock, cmdline, strlen(cmdline)) != 0) { perror("send"); break; }
            /* read one response chunk (server will respond) */
            char buf[4096];
            ssize_t r = recv(sock, buf, sizeof(buf)-1, 0);
            if (r <= 0) { perror("recv"); break; }
            buf[r] = '\0';
            printf("%s", buf);
            if (strncmp(line, "QUIT", 4) == 0) break;
        }
    }

    close(sock);
    return 0;
}
