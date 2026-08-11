#include <stdio.h>
#include <stdbool.h>

// FFmpeg Headers
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

// SDL2 Header
#include <SDL2/SDL.h>

// OS Device Mapping
#if defined(__linux__)
    #define INPUT_FORMAT "v4l2"
    #define DEVICE_NAME  "/dev/video0"
#elif defined(_WIN32)
    #define INPUT_FORMAT "dshow"
    #define DEVICE_NAME  "video=Integrated Camera" // Replace with your exact webcam name
#elif defined(__APPLE__)
    #define INPUT_FORMAT "avfoundation"
    #define DEVICE_NAME  "0"                       // "0" is usually primary camera
#endif

int main(int argc, char *argv[]) {
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVPacket *packet = NULL;
    AVFrame *raw_frame = NULL;
    AVFrame *yuv_frame = NULL;
    struct SwsContext *sws_ctx = NULL;

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    int video_stream_index = -1;
    int response = 0;

    // 1. Register FFmpeg input/output devices
    avdevice_register_all();

    // 2. Locate input format driver for OS
    const AVInputFormat *iformat = av_find_input_format(INPUT_FORMAT);
    if (!iformat) {
        fprintf(stderr, "Error: Could not find input format %s\n", INPUT_FORMAT);
        return -1;
    }

    // Set webcam capture options
    AVDictionary *options = NULL;
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "video_size", "640x480", 0);

    // 3. Open webcam hardware
    printf("Opening camera device: %s...\n", DEVICE_NAME);
    if (avformat_open_input(&fmt_ctx, DEVICE_NAME, iformat, &options) != 0) {
        fprintf(stderr, "Error: Cannot open camera device %s\n", DEVICE_NAME);
        av_dict_free(&options);
        return -1;
    }
    av_dict_free(&options);

    // 4. Find video stream
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Error: Cannot find stream information\n");
        return -1;
    }

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        fprintf(stderr, "Error: No video stream detected\n");
        return -1;
    }

    // 5. Setup decoder
    AVCodecParameters *codec_params = fmt_ctx->streams[video_stream_index]->codecpar;
    codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        fprintf(stderr, "Error: Unsupported codec\n");
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codec_ctx, codec_params) < 0) {
        fprintf(stderr, "Error: Failed to copy codec parameters\n");
        return -1;
    }

    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Error: Could not open decoder\n");
        return -1;
    }

    // 6. Allocate frames & packets
    packet = av_packet_alloc();
    raw_frame = av_frame_alloc();
    yuv_frame = av_frame_alloc();

    int width = codec_ctx->width;
    int height = codec_ctx->height;

    // Allocate memory for converted display frame (YUV420P)
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    uint8_t *yuv_buffer = (uint8_t *)av_malloc(buffer_size);
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, yuv_buffer,
                        AV_PIX_FMT_YUV420P, width, height, 1);

    // 7. Setup SwScale context for frame pixel conversion
    sws_ctx = sws_getContext(
        width, height, codec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL
    );

    // 8. Initialize SDL2 Window & Renderer
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL Initialization Error: %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow("FFmpeg + SDL2 Webcam Stream",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL Window Error: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING, width, height);

    printf("Stream running! Close the window or press ESC/Q to stop.\n");

    // 9. Main Streaming Event Loop
    bool running = true;
    SDL_Event event;

    while (running && av_read_frame(fmt_ctx, packet) >= 0) {
        // Handle GUI window close events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                    running = false;
                }
            }
        }

        if (packet->stream_index == video_stream_index) {
            // Send raw webcam packet to decoder
            if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                while (avcodec_receive_frame(codec_ctx, raw_frame) >= 0) {
                    // Convert frame from camera native format -> YUV420P for rendering
                    sws_scale(sws_ctx,
                              (const uint8_t *const *)raw_frame->data,
                              raw_frame->linesize,
                              0, height,
                              yuv_frame->data,
                              yuv_frame->linesize);

                    // Update SDL Texture with newly converted frame data
                    SDL_UpdateYUVTexture(texture, NULL,
                                         yuv_frame->data[0], yuv_frame->linesize[0],
                                         yuv_frame->data[1], yuv_frame->linesize[1],
                                         yuv_frame->data[2], yuv_frame->linesize[2]);

                    // Render texture to screen
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);
                }
            }
        }
        av_packet_unref(packet);
    }

    // 10. Resource Cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    sws_freeContext(sws_ctx);
    av_free(yuv_buffer);
    av_frame_free(&yuv_frame);
    av_frame_free(&raw_frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    printf("Stream closed cleanly.\n");
    return 0;
}