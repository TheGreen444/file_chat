#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>  

#define PORT 9000
#define BUFSIZE 1024

void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void send_file(int sock, char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    char header[512];
    snprintf(header, sizeof(header), "FILE:%s:%ld\n", base, filesize);
    send(sock, header, strlen(header), 0);

    char buffer[BUFSIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFSIZE, fp)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }

    fclose(fp);
    printf("[+] Sent file: %s (%ld bytes)\n", base, filesize);
}

int recv_line(int sock, char *buf, int size) {
    int i = 0;
    char ch;
    while (i < size - 1) {
        int n = recv(sock, &ch, 1, 0);
        if (n <= 0) return n;
        buf[i++] = ch;
        if (ch == '\n') break;
    }
    buf[i] = '\0';
    return i;
}

void receive_file(int sock, char *header) {
    char filename[256];
    long filesize;

    sscanf(header, "FILE:%255[^:]:%ld", filename, &filesize);

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return;
    }

    long remaining = filesize;
    char buffer[BUFSIZE];
    while (remaining > 0) {
        int bytes = recv(sock, buffer, (remaining < BUFSIZE) ? remaining : BUFSIZE, 0);
        if (bytes <= 0) break;
        fwrite(buffer, 1, bytes, fp);
        remaining -= bytes;
    }

    fclose(fp);
    printf("[✓] Received file: %s (%ld bytes)\n", filename, filesize);
}

int main(int argc, char *argv[]) {
    int sockfd, connfd;
    struct sockaddr_in addr, client;
    socklen_t len = sizeof(client);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s --listen | --connect <IP>\n", argv[0]);
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error_exit("socket");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (strcmp(argv[1], "--listen") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            error_exit("bind");

        listen(sockfd, 1);
        printf("[*] Listening on port %d...\n", PORT);
        connfd = accept(sockfd, (struct sockaddr *)&client, &len);
        if (connfd < 0) error_exit("accept");
        printf("[+] Connection accepted.\n");
    } else if (strcmp(argv[1], "--connect") == 0 && argc == 3) {
        addr.sin_addr.s_addr = inet_addr(argv[2]);
        if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            error_exit("connect");
        connfd = sockfd;
        printf("[+] Connected to server.\n");
    } else {
        fprintf(stderr, "Usage: %s --listen | --connect <IP>\n", argv[0]);
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        char msg[BUFSIZE];
        while (fgets(msg, sizeof(msg), stdin)) {
            if (strncmp(msg, "/send ", 6) == 0) {
                char *filename = msg + 6;
                filename[strcspn(filename, "\n")] = 0; 
                send_file(connfd, filename);
            } else {
                send(connfd, msg, strlen(msg), 0);
            }
        }
        exit(0);
    } else {
        char msg[BUFSIZE + 1];
        while (1) {
            memset(msg, 0, sizeof(msg));
            int n = recv_line(connfd, msg, sizeof(msg));
            if (n <= 0) break;

            if (strncmp(msg, "FILE:", 5) == 0) {
                receive_file(connfd, msg);
            } else {
                printf("Peer: %s", msg);
            }
        }
        printf("[*] Connection closed.\n");
        kill(pid, SIGTERM); //stop child...
    }

    close(connfd);
    close(sockfd);
    return 0;
}

