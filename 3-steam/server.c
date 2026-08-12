#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define PORT 8080
#define BUFFER_SIZE 256

/* ---- Cam ---- */
#define CAM_DEVICE "/dev/video0"
#define CAM_WIDTH  320
#define CAM_HEIGHT 240
#define NUM_BUFS   4

/* ---- Terminal ---- */
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define MAX_COLS 300
#define MAX_ROWS 120
#define MIN_DIM  10
#define AWAIT_TIMEOUT_SEC 1

/* Lightness -> Character. */
static const char RAMP[] = " .:-=+*#%@";
#define RAMP_LEN ((int)(sizeof(RAMP) - 1))

typedef struct {
    void   *start;
    size_t length;
} VideoBuffer;

int client_fd = 0;
int client_cols = DEFAULT_COLS, client_rows = DEFAULT_ROWS;
int client_ready = 0;
time_t client_connected_at = 0;

int cam_fd = -1;
int cam_width = CAM_WIDTH, cam_height = CAM_HEIGHT;
VideoBuffer buffers[NUM_BUFS];

static char framebuf[MAX_ROWS * MAX_COLS * 25 + 1024];

int init_camera(void) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    int i;

    cam_fd = open(CAM_DEVICE, O_RDWR);
    if (cam_fd < 0) {
        perror("open " CAM_DEVICE);
        return -1;
    }

    memset(&cap, 0, sizeof(cap));
    if (ioctl(cam_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "video capture / streaming\n");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_WIDTH;
    fmt.fmt.pix.height = CAM_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "camera didn't return YUYV. Try `v4l2-ctl --list-formats-ext` "
                         "and check available formats.\n");
        return -1;
    }
    cam_width  = fmt.fmt.pix.width;
    cam_height = fmt.fmt.pix.height;

    memset(&req, 0, sizeof(req));
    req.count = NUM_BUFS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    for (i = 0; i < NUM_BUFS; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(cam_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, cam_fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return -1;
        }

        if (ioctl(cam_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF (init)");
            return -1;
        }
    }

    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(cam_fd, VIDIOC_STREAMON, &type) < 0) {
            perror("VIDIOC_STREAMON");
            return -1;
        }
    }

    return 0;
}

/* Extracts the RGB values for a single pixel from the raw YUYV buffer */
static inline void get_rgb(const unsigned char *yuyv, int px, unsigned char *r, unsigned char *g, unsigned char *b) {
    int pair_offset = (px / 2) * 4;
    int y = (px % 2 == 0) ? yuyv[pair_offset] : yuyv[pair_offset + 2];
    int u = yuyv[pair_offset + 1] - 128;
    int v = yuyv[pair_offset + 3] - 128;

    // تحويل YUV -> RGB وحصر القيم بين 0 و 255
    int red   = y + (1402 * v) / 1000;
    int green = y - (344 * u + 714 * v) / 1000;
    int blue  = y + (1772 * u) / 1000;

    *r = (red < 0) ? 0 : ((red > 255) ? 255 : red);
    *g = (green < 0) ? 0 : ((green > 255) ? 255 : green);
    *b = (blue < 0) ? 0 : ((blue > 255) ? 255 : blue);
}

/* builds an ASCII frame of size client_cols x client_rows from the raw camera buffer */
static void render_frame(const unsigned char *yuyv) {
    char *p = framebuf;
    int row, col;

    p += sprintf(p, "\033[H");

    for (row = 0; row < client_rows; row++) {
        int src_y = row * cam_height / client_rows;
        for (col = 0; col < client_cols; col++) {
            int src_x = col * cam_width / client_cols;
            int px = src_y * cam_width + src_x;
            
            unsigned char r, g, b;
            get_rgb(yuyv, px, &r, &g, &b);
            
            /* Calculate the lightness and map it to a character in the RAMP */
            int y = (r + g + b) / 3;
            int idx = (y * (RAMP_LEN - 1)) / 255;

            /* Set the ANSI color code for the character */
            p += sprintf(p, "\033[38;2;%d;%d;%dm%c", r, g, b, RAMP[idx]);
        }
        p += sprintf(p, "\033[K\r\n");
    }
    *p = '\0';
}

void capture_and_send(void) {
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        return;
    }

    if (client_fd > 0 && client_ready) {
        render_frame((unsigned char *)buffers[buf.index].start);
        write(client_fd, framebuf, strlen(framebuf));
    }

    if (ioctl(cam_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
    }
}

/* tries to parse the terminal size from the response on \033[18t in the format \033[8;rows;colst */
static int try_parse_size(const char *buf, int *rows, int *cols) {
    int r, c;
    if (sscanf(buf, "\033[8;%d;%dt", &r, &c) == 2) {
        if (r < MIN_DIM) r = MIN_DIM;
        if (c < MIN_DIM) c = MIN_DIM;
        if (r > MAX_ROWS) r = MAX_ROWS;
        if (c > MAX_COLS) c = MAX_COLS;
        *rows = r;
        *cols = c;
        return 1;
    }
    return 0;
}

static void mark_ready(int rows, int cols) {
    char clear[] = "\033[2J";
    client_rows = rows;
    client_cols = cols;
    client_ready = 1;
    write(client_fd, clear, strlen(clear));
}

static void disconnect_client(void) {
    close(client_fd);
    client_fd = 0;
    client_ready = 0;
}

int main(void) {
    int sockfd, new_fd, max_fd, activity, opt;
    char buffer[BUFFER_SIZE];
    char *query = "\033[2J\033[18t"; /* Size query */
    fd_set readfds;
    struct sockaddr serv;
    struct timeval tv;

    memset(&serv, 0, sizeof(serv));
    serv.sa_family = 2; /* AF_INET */

    /* PORT 8080 (big endian) */
    serv.sa_data[0] = (PORT >> 8) & 0xFF;
    serv.sa_data[1] = PORT & 0xFF;

    /* IP 0.0.0.0 */
    serv.sa_data[2] = 0x00;
    serv.sa_data[3] = 0x00;
    serv.sa_data[4] = 0x00;
    serv.sa_data[5] = 0x00;

    if (init_camera() < 0) {
        fprintf(stderr, "Failed to initialize camera (%s)\n", CAM_DEVICE);
        exit(EXIT_FAILURE);
    }

    sockfd = socket(2 /* AF_INET */, SOCK_STREAM, 0);

    opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sockfd, &serv, sizeof(serv)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    listen(sockfd, 5);
    printf("Server listening Port %d...\n", PORT);

    while (1) {
        printf("waiting for a client...\n");
        new_fd = accept(sockfd, NULL, NULL);

        client_fd = new_fd;
        client_ready = 0;
        client_cols = DEFAULT_COLS;
        client_rows = DEFAULT_ROWS;
        client_connected_at = time(NULL);
        write(client_fd, query, strlen(query));

        while (client_fd > 0) {
            FD_ZERO(&readfds);
            FD_SET(cam_fd, &readfds);
            FD_SET(client_fd, &readfds);
            max_fd = cam_fd > client_fd ? cam_fd : client_fd;

            tv.tv_sec = 1;
            tv.tv_usec = 0;

            activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
            if (activity < 0) continue;

            if (FD_ISSET(cam_fd, &readfds)) {
                capture_and_send();
            }

            if (FD_ISSET(client_fd, &readfds)) {
                int readed = read(client_fd, buffer, BUFFER_SIZE - 1);

                if (readed <= 0) {
                    disconnect_client();
                    break;
                }

                buffer[readed] = '\0';

                if (!client_ready) {
                    int r, c;
                    if (try_parse_size(buffer, &r, &c)) {
                        mark_ready(r, c);
                    } else {
                        mark_ready(DEFAULT_ROWS, DEFAULT_COLS);
                    }
                } else if (buffer[0] == 'q') {
                    disconnect_client();
                    break;
                }
            } else if (!client_ready &&
                       (time(NULL) - client_connected_at) > AWAIT_TIMEOUT_SEC) {
                /* Terminal didn't respond to the query, use default size */
                mark_ready(DEFAULT_ROWS, DEFAULT_COLS);
            }
        }
    }
    return 0;
}
