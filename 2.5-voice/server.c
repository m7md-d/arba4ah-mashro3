#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>

#define PORT 8080

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024 * 8
#define HISTORY_SIZE 16384

/**
 * Structure to hold client information
 */
typedef struct {
    int fd;
    char username[32];
    int registered;
} Client;

static struct {
    int voice_n;
    int minits;
    char sec;
} voice_note = {0};

static struct {
    int voice_n;
    int minits;
    char sec;
} voice_notes_array[MAX_CLIENTS];

Client clients[MAX_CLIENTS];

int selected_index = 0;

/**
 * Disables canonical mode and echo for terminal input, allowing for raw input handling.
 */
void enable_raw_mode(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void list_voice_notes(void) {
    printf("\033[H\033[J");
    printf("--- Voice Notes List (%d) ---\n", voice_note.voice_n);
    
    for (int i = 0; i < voice_note.voice_n; i++) {
        if (i == selected_index) {
            
            printf("\033[7m > voice_note_%02d.wav | %02d:%02d \033[0m\n", 
                   i, voice_notes_array[i].minits, voice_notes_array[i].sec);
        } else {
            printf("   voice_note_%02d.wav | %02d:%02d\n", 
                   i, voice_notes_array[i].minits, voice_notes_array[i].sec);
        }
    }
}

int wav_reader(int wav_file) {
    unsigned char header[44];
    uint32_t byte_rate, data_size, total_seconds;

    lseek(wav_file, 0, SEEK_SET);
    if (read(wav_file, header, sizeof(header)) != sizeof(header)) {
        perror("Error reading WAV header");
        return -1;
    }
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F' ||
        header[8] != 'W' || header[9] != 'A' || header[10] != 'V' || header[11] != 'E') {
        fprintf(stderr, "Invalid WAV file format\n");
        return -1;
    }
    /* Calculate voice note time */
    byte_rate = header[28] | (header[29] << 8) | ((uint32_t)header[30] << 16) | ((uint32_t)header[31] << 24);
    data_size = header[40] | (header[41] << 8) | ((uint32_t)header[42] << 16) | ((uint32_t)header[43] << 24);

    if (byte_rate > 0) {
        total_seconds = data_size / byte_rate;
        voice_note.minits = total_seconds / 60;
        voice_note.sec = total_seconds % 60;
    }

    return 0;
}

int write_voice(int new_client, char *buffer) {
    int voice_file;
    ssize_t readed;

    sprintf(buffer, "voice_notes/voice_note_%02d.wav", voice_note.voice_n);
    voice_file = open(buffer, O_RDWR | O_CREAT | O_TRUNC, 0644);

    if (voice_file < 0) {
        perror("Error creating file");
        close(new_client);
        return (-1);
    }

    while((readed = read(new_client, buffer, BUFFER_SIZE - 1)) > 0) {
        write(voice_file, buffer, readed);
    }
    if (readed < 0) {
        perror("Error reading from client");
        close(voice_file);
        close(new_client);
        sprintf(buffer, "voice_notes/voice_note_%02d.wav", voice_note.voice_n);
        remove(buffer);

        return (-1);
    }
    wav_reader(voice_file);
    voice_notes_array[voice_note.voice_n].voice_n = voice_note.voice_n;
    voice_notes_array[voice_note.voice_n].minits = voice_note.minits;
    voice_notes_array[voice_note.voice_n].sec = voice_note.sec;
    voice_note.voice_n++;
    close(voice_file);
    close(new_client);
    list_voice_notes();

    return (0);
}

int play_voice_note(int note_index) {
    int voice_file;
    char filename[256];
    ssize_t readed;

    sprintf(filename, "voice_notes/voice_note_%02d.wav", note_index);
    if(!fork()) {
        #ifdef __linux__
        execlp("aplay", "aplay", filename, NULL);
        #elif __APPLE__
        execlp("afplay", "afplay", filename, NULL);
        #endif
        exit(0);
    }

    return (0);
}

void user_input_handler(void) {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) > 0) {
        if (ch == 27) { /* ESC key */
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                if (seq[0] == '[') {
                    if (seq[1] == 'A') { /* Up arrow */
                        if (selected_index > 0) selected_index--;
                    } else if (seq[1] == 'B') { /* Down arrow */
                        if (selected_index < voice_note.voice_n - 1) selected_index++;
                    }
                }
            }
        } else if (ch == '\n' || ch == '\r') {
            play_voice_note(selected_index);
        }
    }
    list_voice_notes();
}

int main() {
    int sockfd, new_client, max_fd, activity ,i, opt, sd;
    char buffer[BUFFER_SIZE], cmd_buffer[BUFFER_SIZE];
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

    enable_raw_mode();
    mkdir("voice_notes", 0777);

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

    listen(sockfd, 5 /* backlog */);
    printf("Server listening Port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        max_fd = sockfd;

        /* Add existing clients to the read set */
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].fd;
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_fd) max_fd = sd;
        }

        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) continue; /* Error occurred */

        if (FD_ISSET(0, &readfds))
            user_input_handler();
            

        /* Check for new incoming connections */
        if (FD_ISSET(sockfd, &readfds)) {
            new_client = accept(sockfd, NULL, NULL);
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == 0) {
                    write_voice(new_client, buffer);
                    break;
                }
            }
        }

        /* Check for input from existing clients */
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].fd;

            /* If the client socket is ready for reading */
            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                /* Process the received message */
                write_voice(sd, buffer);
            }
        }
    }
    return 0;
}
