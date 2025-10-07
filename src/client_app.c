#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

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

static ssize_t read_line(int fd, char *buf, size_t maxlen) {
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

    printf("Connected successfully!\n");
    printf("\nCommands available:\n");
    printf("  SIGNUP <user> <pass>\n");
    printf("  LOGIN <user> <pass>\n");
    printf("  UPLOAD <filename>\n");
    printf("  DOWNLOAD <filename>\n");
    printf("  LIST\n");
    printf("  DELETE <filename>\n");
    printf("  QUIT\n\n");

    char line[1024];
    int logged_in = 0;
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) break;
        
        /* remove newline */
        size_t L = strlen(line);
        if (L && line[L-1] == '\n') line[L-1] = '\0';
        
        if (strlen(line) == 0) continue;
        
        /* Parse command */
        char cmd[32] = {0};
        sscanf(line, "%31s", cmd);
        
        if (strcmp(cmd, "QUIT") == 0) {
            char quitcmd[64];
            snprintf(quitcmd, sizeof(quitcmd), "QUIT\n");
            send_all(sock, quitcmd, strlen(quitcmd));
            
            char reply[256];
            ssize_t r = read_line(sock, reply, sizeof(reply));
            if (r > 0) {
                printf("%s", reply);
            }
            break;
            
        } else if (strcmp(cmd, "SIGNUP") == 0 || strcmp(cmd, "LOGIN") == 0) {
            /* Send command with newline */
            char cmdline[512];
            snprintf(cmdline, sizeof(cmdline), "%s\n", line);
            if (send_all(sock, cmdline, strlen(cmdline)) != 0) {
                perror("send");
                break;
            }
            
            /* Read response */
            char reply[256];
            ssize_t r = read_line(sock, reply, sizeof(reply));
            if (r <= 0) {
                perror("recv");
                break;
            }
            printf("%s", reply);
            
            if (strcmp(cmd, "LOGIN") == 0 && strncmp(reply, "OK", 2) == 0) {
                logged_in = 1;
            }
            
        } else if (strcmp(cmd, "UPLOAD") == 0) {
            if (!logged_in) {
                printf("ERR: Please login first\n");
                continue;
            }
            
            char filename[256];
            if (sscanf(line + 7, "%255s", filename) != 1) {
                printf("Usage: UPLOAD <filename>\n");
                continue;
            }
            
            /* Open file */
            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                perror("fopen");
                continue;
            }
            
            /* Get file size */
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (sz < 0) {
                fclose(fp);
                printf("Cannot stat file\n");
                continue;
            }
            
            /* Send command with size */
            char header[512];
            snprintf(header, sizeof(header), "UPLOAD %s %ld\n", filename, sz);
            if (send_all(sock, header, strlen(header)) != 0) {
                perror("send");
                fclose(fp);
                break;
            }
            
            /* Wait for READY */
            char ready[256];
            ssize_t r = read_line(sock, ready, sizeof(ready));
            if (r <= 0 || strncmp(ready, "READY", 5) != 0) {
                printf("Server not ready: %s\n", ready);
                fclose(fp);
                continue;
            }
            
            /* Send file bytes */
            char buf[4096];
            size_t total_sent = 0;
            while (total_sent < (size_t)sz) {
                size_t to_read = sizeof(buf);
                if (to_read > (size_t)sz - total_sent) {
                    to_read = (size_t)sz - total_sent;
                }
                size_t rr = fread(buf, 1, to_read, fp);
                if (rr == 0) break;
                if (send_all(sock, buf, rr) != 0) {
                    perror("send");
                    fclose(fp);
                    goto cleanup;
                }
                total_sent += rr;
            }
            fclose(fp);
            
            /* Read response after worker completes */
            char res[256];
            ssize_t s = read_line(sock, res, sizeof(res));
            if (s > 0) {
                printf("%s", res);
            } else {
                perror("recv response");
                break;
            }
            
        } else if (strcmp(cmd, "DOWNLOAD") == 0) {
            if (!logged_in) {
                printf("ERR: Please login first\n");
                continue;
            }
            
            char filename[256];
            if (sscanf(line + 9, "%255s", filename) != 1) {
                printf("Usage: DOWNLOAD <filename>\n");
                continue;
            }
            
            /* Send command */
            char header[512];
            snprintf(header, sizeof(header), "DOWNLOAD %s\n", filename);
            if (send_all(sock, header, strlen(header)) != 0) {
                perror("send");
                break;
            }
            
            /* Read response header */
            char resp[256];
            ssize_t r = read_line(sock, resp, sizeof(resp));
            if (r <= 0) {
                perror("recv");
                break;
            }
            
            if (strncmp(resp, "OK download ", 12) == 0) {
                size_t size = 0;
                sscanf(resp + 12, "%zu", &size);
                
                /* Receive exact size */
                char *buf = malloc(size + 1);
                if (!buf) {
                    printf("alloc fail\n");
                    break;
                }
                
                ssize_t got = read_n_bytes(sock, buf, size);
                if (got != (ssize_t)size) {
                    printf("incomplete download (got %zd, expected %zu)\n", got, size);
                    free(buf);
                    break;
                }
                
                buf[size] = '\0';
                
                /* Save to file */
                FILE *fp = fopen(filename, "wb");
                if (!fp) {
                    perror("fopen");
                    free(buf);
                    continue;
                }
                fwrite(buf, 1, size, fp);
                fclose(fp);
                free(buf);
                
                printf("Downloaded %s (%zu bytes)\n", filename, size);
            } else {
                printf("%s", resp);
            }
            
        } else if (strcmp(cmd, "LIST") == 0) {
            if (!logged_in) {
                printf("ERR: Please login first\n");
                continue;
            }
            
            /* Send command */
            char cmdline[64];
            snprintf(cmdline, sizeof(cmdline), "LIST\n");
            if (send_all(sock, cmdline, strlen(cmdline)) != 0) {
                perror("send");
                break;
            }
            
            /* Read response header */
            char resp[256];
            ssize_t r = read_line(sock, resp, sizeof(resp));
            if (r <= 0) {
                perror("recv");
                break;
            }
            
            if (strncmp(resp, "OK list ", 8) == 0) {
                size_t size = 0;
                sscanf(resp + 8, "%zu", &size);
                
                if (size == 0) {
                    printf("(empty directory)\n");
                } else {
                    /* Receive list data */
                    char *buf = malloc(size + 1);
                    if (!buf) {
                        printf("alloc fail\n");
                        break;
                    }
                    
                    ssize_t got = read_n_bytes(sock, buf, size);
                    if (got != (ssize_t)size) {
                        printf("incomplete list\n");
                        free(buf);
                        break;
                    }
                    
                    buf[size] = '\0';
                    printf("Files:\n%s", buf);
                    free(buf);
                }
            } else {
                printf("%s", resp);
            }
            
        } else if (strcmp(cmd, "DELETE") == 0) {
            if (!logged_in) {
                printf("ERR: Please login first\n");
                continue;
            }
            
            char filename[256];
            if (sscanf(line + 7, "%255s", filename) != 1) {
                printf("Usage: DELETE <filename>\n");
                continue;
            }
            
            /* Send command */
            char cmdline[512];
            snprintf(cmdline, sizeof(cmdline), "DELETE %s\n", filename);
            if (send_all(sock, cmdline, strlen(cmdline)) != 0) {
                perror("send");
                break;
            }
            
            /* Read response */
            char resp[256];
            ssize_t r = read_line(sock, resp, sizeof(resp));
            if (r > 0) {
                printf("%s", resp);
            } else {
                perror("recv");
                break;
            }
            
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }

cleanup:
    close(sock);
    return 0;
}