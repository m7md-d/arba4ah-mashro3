#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 8080

#define BUFFER_SIZE 1024
#define HISTORY_SIZE 16384

int selected_index = 0;
int file_count = 0;
char files_array[100][256];

/**
 * Refreshes the list of files in the "files" directory.
 */
void refresh_file_list(void) {
    file_count = 0;
    FILE *fp;
    char path[256];

    /* Assuming files are in the "files" directory */
    fp = popen("ls files", "r");
    if (fp == NULL) {
        perror("Failed to list files");
        return;
    }

    while (fgets(path, sizeof(path), fp) != NULL && file_count < 100) {
        path[strcspn(path, "\n")] = 0; // Remove newline character
        strncpy(files_array[file_count], path, sizeof(files_array[file_count]) - 1);
        files_array[file_count][sizeof(files_array[file_count]) - 1] = '\0'; // Ensure null-termination
        file_count++;
    }

    pclose(fp);
}

/**
 * Sends the user interface to the client.
 */
void send_ui(int client_fd) {
    char ui_buffer[BUFFER_SIZE];
    int offset = 0;

    offset += snprintf(ui_buffer + offset, sizeof(ui_buffer) - offset, "\033[H\033[J");
    offset += snprintf(ui_buffer + offset, sizeof(ui_buffer) - offset, "--- Files List (%d) ---\r\n", file_count);

    for (int i = 0; i < file_count; i++) {
        if (i == selected_index) {
            offset += snprintf(ui_buffer + offset, sizeof(ui_buffer) - offset, "\033[7m > %s \033[0m\r\n", files_array[i]);
        } else {
            offset += snprintf(ui_buffer + offset, sizeof(ui_buffer) - offset, "   %s\r\n", files_array[i]);
        }
    }

    offset += snprintf(ui_buffer + offset, sizeof(ui_buffer) - offset, "\n----------------------------------\r\n");
    offset += snprintf(ui_buffer + offset, sizeof(ui_buffer) - offset, " [u] Upload   [d] Download   [q] Quit\r\n");

    write(client_fd, ui_buffer, strlen(ui_buffer));
}

/**
 * Handles input from the client.
 */
void handle_input(int client_fd, char *buffer, ssize_t read_len) {
    if (read_len <= 0) return;

    if (read_len >= 3 && buffer[0] == 27 && buffer[1] == '[') {
        if (buffer[2] == 'A') {
            if (selected_index > 0) selected_index--;
        } else if (buffer[2] == 'B') {
            if (selected_index < file_count - 1) selected_index++;
        }
    } 
    else if (buffer[0] == '\n' || buffer[0] == '\r') {
        char msg[256];
        snprintf(msg, sizeof(msg), "\033[H\033[J You selected: %s\n", files_array[selected_index]);
        write(client_fd, msg, strlen(msg));
        return;
    }
    
    /* handle other commands */
    else if (buffer[0] == 'u' || buffer[0] == 'U') {
        char msg[] = "\033[H\033[J --- Upload Mode ---\nWaiting for file data...\n";
        write(client_fd, msg, strlen(msg));
        /* Upload logic will be added later */
        return;
    } 
    else if (buffer[0] == 'd' || buffer[0] == 'D') {
        char msg[256];
        snprintf(msg, sizeof(msg), "\033[H\033[J --- Download Mode ---\nDownloading: %s\n", files_array[selected_index]);
        write(client_fd, msg, strlen(msg));
        /* Download logic will be added later */
        return;
    }
    else if (buffer[0] == 'q' || buffer[0] == 'Q') {
        close(client_fd);
        return;
    }

    send_ui(client_fd);
}

int main(int argc, char **argv) {
    int sockfd, new_client = 0, max_fd, activity ,i, opt, sd;
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    struct sockaddr serv;
    int port = 8080;
    unsigned char ip[4] = {0, 0, 0, 0};

    serv.sa_family = 2;     /* 2 = AF_INET */

    if (argc > 1) /* IP */
        sscanf(argv[1], "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]);
    if (argc > 2) /* PORT */
        port = atoi(argv[2]);

    /* PORT Default 8080 */
    serv.sa_data[0] = (port >> 8) & 0xFF;
    serv.sa_data[1] = port & 0xFF;

    /* IP Default 0.0.0.0 */
    serv.sa_data[2] = ip[0];
    serv.sa_data[3] = ip[1];
    serv.sa_data[4] = ip[2];
    serv.sa_data[5] = ip[3];

    #ifdef __APPLE__
    serv.sa_len = 16; /* For macOS, the length of the sockaddr structure is 16 bytes */
    #endif

    sockfd = socket(serv.sa_family, SOCK_STREAM, 0);

    opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    #ifdef __APPLE__
    if (connect(sockfd, &serv, serv.sa_len) < 0) {
    #else
    if (connect(sockfd, &serv, sizeof(serv)) < 0) {
    #endif
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    listen(sockfd, 5 /* backlog */);
    printf("Server listening Port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        max_fd = sockfd;

        if (new_client > 0) {
            FD_SET(new_client, &readfds);
            if (new_client > max_fd) max_fd = new_client;
        }

        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) continue;

        /* Check for new incoming connections */
        if (FD_ISSET(sockfd, &readfds)) {
            int temp_fd = accept(sockfd, NULL, NULL);
            if (new_client == 0) {
                new_client = temp_fd;
                refresh_file_list();
                send_ui(new_client);
            } else {
                close(temp_fd); 
            }
        }

        /* Check for input from the connected client */
        if (new_client > 0 && FD_ISSET(new_client, &readfds)) {
            ssize_t read_len = read(new_client, buffer, BUFFER_SIZE - 1);
            if (read_len <= 0) {
                close(new_client);
                new_client = 0;
            } else {
                handle_input(new_client, buffer, read_len);
            }
        }
    }
    return 0;
}
