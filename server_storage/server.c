#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include "../include/dropbox.h"

void receive_file(int new_socket, char *filename) {
    char buffer[BUFFER_SIZE];
    char path[256];
    // Strip directory from filename
    char *base = strrchr(filename, '/');
    if (base) {
        base++;
    } else {
        base = filename;
    }
    snprintf(path, sizeof(path), "server_storage/%s", base);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror("File open failed");
        return;
    }

    int bytes_read;
    while ((bytes_read = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
        if (strncmp(buffer, "DONE", 4) == 0)
            break;
        fwrite(buffer, 1, bytes_read, fp);
    }
    fclose(fp);
    printf("File '%s' received and saved.\n", filename);
}

void send_file(int new_socket, char *filename) {
    char buffer[BUFFER_SIZE];
    char path[256];
    // Strip directory from filename
    char *base = strrchr(filename, '/');
    if (base) {
        base++;
    } else {
        base = filename;
    }
    snprintf(path, sizeof(path), "server_storage/%s", base);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        send(new_socket, "ERROR", strlen("ERROR"), 0);
        perror("File not found");
        return;
    }

    int bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
        send(new_socket, buffer, bytes_read, 0);

    send(new_socket, "DONE", 4, 0);
    fclose(fp);
    printf("File '%s' sent to client.\n", filename);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    mkdir("server_storage", 0777);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR , &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server ready. Listening on port %d...\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        memset(buffer, 0, BUFFER_SIZE);
        read(new_socket, buffer, BUFFER_SIZE);

        char command[16], filename[128];
        sscanf(buffer, "%s %s", command, filename);

        if (strcmp(command, "UPLOAD") == 0) {
            send(new_socket, "READY", strlen("READY"), 0);
            receive_file(new_socket, filename);
        } else if (strcmp(command, "DOWNLOAD") == 0) {
            send_file(new_socket, filename);
        } else {
            send(new_socket, "INVALID", strlen("INVALID"), 0);
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}
DONE