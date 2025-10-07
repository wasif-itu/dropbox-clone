#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../include/dropbox.h"

void upload_file(int sock, char *filename) {
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("File not found.\n");
        return;
    }

    snprintf(buffer, sizeof(buffer), "UPLOAD %s", filename);
    send(sock, buffer, strlen(buffer), 0);
    memset(buffer, 0, BUFFER_SIZE);
    read(sock, buffer, BUFFER_SIZE);

    if (strncmp(buffer, "READY", 5) != 0) {
        printf("Server not ready for upload.\n");
        fclose(fp);
        return;
    }

    int bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
        send(sock, buffer, bytes_read, 0);

    send(sock, "DONE", 4, 0);
    fclose(fp);
    printf("File '%s' uploaded successfully.\n", filename);
}

void download_file(int sock, char *filename) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "DOWNLOAD %s", filename);
    send(sock, buffer, strlen(buffer), 0);

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Error creating file.\n");
        return;
    }

    int bytes_read;
    while ((bytes_read = read(sock, buffer, BUFFER_SIZE)) > 0) {
        if (strncmp(buffer, "DONE", 4) == 0)
            break;
        if (strncmp(buffer, "ERROR", 5) == 0) {
            printf("Server: File not found.\n");
            fclose(fp);
            remove(filename);
            return;
        }
        fwrite(buffer, 1, bytes_read, fp);
    }

    fclose(fp);
    printf("File '%s' downloaded successfully.\n", filename);
}

void list_files(int sock) {
    char buffer[BUFFER_SIZE];
    send(sock, "LIST", strlen("LIST"), 0);
    printf("Files on server:\n");

    int bytes_read;
    while ((bytes_read = read(sock, buffer, BUFFER_SIZE)) > 0) {
        buffer[bytes_read] = '\0';
        if (strncmp(buffer, "DONE", 4) == 0)
            break;
        if (strncmp(buffer, "ERROR", 5) == 0) {
            printf("Error listing files.\n");
            return;
        }
        printf("%s", buffer);
    }
}

int main() {
    int sock;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/Address not supported\n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed\n");
        return -1;
    }

    int choice;
    char filename[128];
    printf("1. Upload file\n2. Download file\n3. List files\nEnter choice: ");
    scanf("%d", &choice);

    if (choice == 1) {
        printf("Enter filename to upload: ");
        scanf("%s", filename);
        upload_file(sock, filename);
    } else if (choice == 2) {
        printf("Enter filename to download: ");
        scanf("%s", filename);
        download_file(sock, filename);
    } else if (choice == 3) {
        list_files(sock);
    } else {
        printf("Invalid choice.\n");
    }

    close(sock);
    return 0;
}
