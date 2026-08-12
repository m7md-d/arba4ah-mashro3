#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 8080

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define HISTORY_SIZE 16384

/**
 * Structure to hold client information
 */
typedef struct {
    int fd;
    char username[32];
    int registered;
} Client;

Client clients[MAX_CLIENTS];
char history[HISTORY_SIZE] = "";

/**
 * Appends a message to the chat history
 */
void append_to_history(const char *msg) {
    if (strlen(history) + strlen(msg) < HISTORY_SIZE - 1) {
        strcat(history, msg);
    }
}

/**
 * Broadcasts a message to all connected clients except the one specified by exclude_fd
 */
void broadcast(const char *msg, int exclude_fd) {
    int i;
    char formatted_out[BUFFER_SIZE + 128];

    snprintf(formatted_out, sizeof(formatted_out), "\r\033[K%sYou> ", msg);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0 && clients[i].registered && clients[i].fd != exclude_fd) {
            write(clients[i].fd, formatted_out, strlen(formatted_out));
        }
    }
}

int main() {
    int sockfd, new_client, max_fd, activity ,i, opt, sd;
    char buffer[BUFFER_SIZE], join_msg[128], leave_msg[128], formatted[BUFFER_SIZE + 64];
    char *prompt = "Enter your username: ",
         *hdr = "--- History ---\n",
         *ftr = "---------------\n";
    fd_set readfds;
    struct sockaddr serv;

    serv.sa_family = 2;     /* 2 = AF_INET */

    /* PORT 8080 */
    serv.sa_data[0] = 0x1f;
    serv.sa_data[1] = 0x90;

    /* IP 0.0.0.0 */
    serv.sa_data[2] = 0x00;
    serv.sa_data[3] = 0x00;
    serv.sa_data[4] = 0x00;
    serv.sa_data[5] = 0x00;

    serv.sa_len = 16;

    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = 0;
        clients[i].registered = 0;
    }

    sockfd = socket(serv.sa_family, SOCK_STREAM, 0);

    opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sockfd, &serv, serv.sa_len) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    listen(sockfd, 5);
    printf("Server listening Port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        max_fd = sockfd;

        /* Add existing clients to the read set */
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].fd;
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_fd) max_fd = sd;
        }

        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) continue; /* Error occurred */

        /* Check for new incoming connections */
        if (FD_ISSET(sockfd, &readfds)) {
            new_client = accept(sockfd, NULL, NULL);
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == 0) {
                    clients[i].fd = new_client;
                    clients[i].registered = 0;

                    write(new_client, prompt, strlen(prompt));
                    break;
                }
            }
        }

        /* Check for input from existing clients */
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].fd;

            /* If the client socket is ready for reading */
            if (FD_ISSET(sd, &readfds)) {
                short readed = read(sd, buffer, BUFFER_SIZE - 2);

                /* If read returns 0 or negative, the client has disconnected */
                if (readed <= 0) {
                    if (clients[i].registered) {
                        snprintf(leave_msg, sizeof(leave_msg), "*** %s left the room ***\n", clients[i].username);
                        broadcast(leave_msg, sd);
                        write(clients[i].fd, "You> ", 5);
                        append_to_history(leave_msg);
                    }
                    close(sd);
                    clients[i].fd = 0;
                    clients[i].registered = 0;
                } else {
                    buffer[readed] = '\0';
                    buffer[strcspn(buffer, "\r\n")] = 0;

                    if (strlen(buffer) == 0) continue;

                    /* If the client is not registered, treat the input as a username */
                    if (!clients[i].registered) {
                        strncpy(clients[i].username, buffer, 31);
                        clients[i].registered = 1;

                        if (strlen(history) > 0) {
                            write(sd, hdr, strlen(hdr));
                            write(sd, history, strlen(history));
                            write(sd, ftr, strlen(ftr));
                        }


                        snprintf(join_msg, sizeof(join_msg), "*** %s joined ***\n", clients[i].username);
                        broadcast(join_msg, sd);
                        write(clients[i].fd, "You> ", 5);
                        append_to_history(join_msg);
                    } else {
                        snprintf(formatted, sizeof(formatted), "[%s]: %s\n", clients[i].username, buffer);
                        append_to_history(formatted);
                        broadcast(formatted, sd);
                        write(clients[i].fd, "You> ", 5);
                    }
                }
            }
        }
    }
    return 0;
}
