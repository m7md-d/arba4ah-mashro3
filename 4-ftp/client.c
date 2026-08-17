#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/stat.h>
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

/**
 * Prompts for a local file path and streams it to the server as
 * "<name> <size>\n" followed by the raw bytes.
 */
void upload_file(int sockfd) {
    char path[256], header[300], buffer[BUFFER_SIZE];
    const char *name;
    long size;
    int len;
    size_t readed;
    FILE *fp;

    disable_raw_mode();
    printf("\r\nLocal file to upload: ");
    fflush(stdout);

    if (fgets(path, sizeof(path), stdin) == NULL) {
        enable_raw_mode();
        return;
    }
    path[strcspn(path, "\n")] = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        enable_raw_mode();
        return;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    name = strrchr(path, '/');
    name = name ? name + 1 : path;

    len = snprintf(header, sizeof(header), "%s %ld\n", name, size);
    write(sockfd, header, len);

    while ((readed = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        write(sockfd, buffer, readed);
    }

    fclose(fp);
    enable_raw_mode();
}

/**
 * Reads one byte, taking it from the already-received prefix first
 * and falling back to the socket once the prefix is exhausted.
 */
static int next_byte(int sockfd, const char *prefix, int prefix_len, int *idx, char *out) {
    if (*idx < prefix_len) {
        *out = prefix[(*idx)++];
        return 1;
    }
    return read(sockfd, out, 1) == 1;
}

/**
 * Parses the "<name> <size>\n" header (the \x01 marker was already
 * consumed by the caller) and saves the following <size> bytes under
 * downloads/<name>. Returns how many bytes of prefix belonged to the
 * header+file, so the caller can print whatever comes after them.
 */
int receive_download(int sockfd, const char *prefix, int prefix_len) {
    char header[300], path[300], buffer[BUFFER_SIZE], name[256], c;
    long size, remaining;
    int hlen = 0, idx = 0, chunk;
    ssize_t readed;
    FILE *fp;

    while (hlen < (int)sizeof(header) - 1 && next_byte(sockfd, prefix, prefix_len, &idx, &c)) {
        if (c == '\n') break;
        header[hlen++] = c;
    }
    header[hlen] = '\0';

    if (sscanf(header, "%255s %ld", name, &size) != 2) return idx;

    mkdir("downloads", 0777);
    snprintf(path, sizeof(path), "downloads/%s", name);
    fp = fopen(path, "wb");
    if (!fp) {
        perror("fopen");
        return idx;
    }

    remaining = size;

    /* Payload bytes that arrived in the same read() as the header */
    if (idx < prefix_len) {
        chunk = prefix_len - idx;
        if (chunk > remaining) chunk = remaining;
        fwrite(prefix + idx, 1, chunk, fp);
        remaining -= chunk;
        idx += chunk;
    }

    while (remaining > 0 &&
           (readed = read(sockfd, buffer, remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE)) > 0) {
        fwrite(buffer, 1, readed, fp);
        remaining -= readed;
    }

    fclose(fp);
    printf("\r\nSaved to %s\r\n", path);
    return idx;
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
            char *marker;

            read_len = read(sockfd, buffer, BUFFER_SIZE - 1);
            if (read_len <= 0) break;

            /* A \x01 byte marks the start of incoming file data */
            marker = memchr(buffer, 1, read_len);
            if (marker) {
                int prefix_len = read_len - (int)(marker - buffer) - 1;
                int consumed = receive_download(sockfd, marker + 1, prefix_len);
                int tail_off = (int)(marker - buffer) + 1 + consumed;

                write(STDOUT_FILENO, buffer, marker - buffer);
                if (tail_off < read_len) {
                    write(STDOUT_FILENO, buffer + tail_off, read_len - tail_off);
                }
            } else {
                write(STDOUT_FILENO, buffer, read_len);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            read_len = read(STDIN_FILENO, buffer, BUFFER_SIZE);
            if (read_len > 0) {
                write(sockfd, buffer, read_len);
                if (buffer[0] == 'u' || buffer[0] == 'U') {
                    upload_file(sockfd);
                } else if (buffer[0] == 'q' || buffer[0] == 'Q') {
                    break;
                }
            }
        }
    }

    close(sockfd);
    return 0;
}
