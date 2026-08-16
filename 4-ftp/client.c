#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <termios.h>

#define PORT 8080
#define BUFFER_SIZE 1024

struct termios orig_termios;

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

int main(int argc, char **argv) {
    int sockfd, max_fd, activity;
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    struct sockaddr serv;
    ssize_t read_len;
    int port = 8080;
    unsigned char ip[4] = {127, 0, 0, 1};

    serv.sa_family = 2;     /* 2 = AF_INET */

    if (argc > 1) /* IP */
        sscanf(argv[1], "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]);
    if (argc > 2) /* PORT */
        port = atoi(argv[2]);

    /* PORT Default 8080 */
    serv.sa_data[0] = (port >> 8) & 0xFF;
    serv.sa_data[1] = port & 0xFF;

    /* IP Default 127.0.0.1 */
    serv.sa_data[2] = ip[0];
    serv.sa_data[3] = ip[1];
    serv.sa_data[4] = ip[2];
    serv.sa_data[5] = ip[3];

    #ifdef __APPLE__
    serv.sa_len = 16; /* For macOS, the length of the sockaddr structure is 16 bytes */
    #endif

    sockfd = socket(serv.sa_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    #ifdef __APPLE__
    if (connect(sockfd, &serv, serv.sa_len) < 0) {
    #else
    if (connect(sockfd, &serv, sizeof(serv)) < 0) {
    #endif
        perror("connect failed");
        exit(EXIT_FAILURE);
    }
    

    enable_raw_mode();

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);

        max_fd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) continue;

        if (FD_ISSET(sockfd, &readfds)) {
            read_len = read(sockfd, buffer, BUFFER_SIZE - 1);
            if (read_len <= 0) break;
            
            buffer[read_len] = '\0';
            write(STDOUT_FILENO, buffer, read_len);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            read_len = read(STDIN_FILENO, buffer, BUFFER_SIZE);
            if (read_len > 0) {
                write(sockfd, buffer, read_len);
                if (buffer[0] == 'q' || buffer[0] == 'Q') break;
            }
        }
    }

    close(sockfd);
    return 0;
}
