#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

#define REQUESTED_RATE 44100
#define CHANNELS 1
#define BITS_PER_SAMPLE 16
#define FRAMES_PER_READ 128

typedef struct __attribute__((packed)) {
    char     riff_header[4];
    uint32_t wav_size;
    char     wave_header[4];
    char     fmt_header[4];
    uint32_t fmt_chunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data_header[4];
    uint32_t data_bytes;
} WavHeader;

void write_wav_header(FILE *file, uint32_t total_data_bytes, uint32_t actual_rate) {
    WavHeader header;
    memcpy(header.riff_header, "RIFF", 4);
    header.wav_size = total_data_bytes + sizeof(WavHeader) - 8;
    memcpy(header.wave_header, "WAVE", 4);
    memcpy(header.fmt_header, "fmt ", 4);
    header.fmt_chunk_size = 16;
    header.audio_format = 1; // PCM
    header.num_channels = CHANNELS;
    header.sample_rate = actual_rate;
    header.byte_rate = actual_rate * CHANNELS * (BITS_PER_SAMPLE / 8);
    header.block_align = CHANNELS * (BITS_PER_SAMPLE / 8);
    header.bits_per_sample = BITS_PER_SAMPLE;
    memcpy(header.data_header, "data", 4);
    header.data_bytes = total_data_bytes;

    fseek(file, 0, SEEK_SET);
    fwrite(&header, sizeof(WavHeader), 1, file);
    fflush(file);
}

int main() {
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    unsigned int rate = REQUESTED_RATE;
    int err;

    FILE *wav_file = fopen("output.wav", "wb");
    if (!wav_file) {
        perror("Failed to open output file");
        return 1;
    }

    // Reserve 44 bytes for header
    WavHeader dummy_header = {0};
    fwrite(&dummy_header, sizeof(WavHeader), 1, wav_file);

    // Open capture device
    if ((err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Cannot open audio device: %s\n", snd_strerror(err));
        fclose(wav_file);
        return 1;
    }

    // Allocate hardware parameters
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);

    // Set hardware parameters with error checking
    if ((err = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
        (err = snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE)) < 0 ||
        (err = snd_pcm_hw_params_set_channels(pcm_handle, params, CHANNELS)) < 0 ||
        (err = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0)) < 0 ||
        (err = snd_pcm_hw_params(pcm_handle, params)) < 0) {
        fprintf(stderr, "Failed to set ALSA parameters: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        fclose(wav_file);
        return 1;
    }

    // Prepare capture interface
    if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
        fprintf(stderr, "Cannot prepare audio interface: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        fclose(wav_file);
        return 1;
    }

    int frame_size = CHANNELS * (BITS_PER_SAMPLE / 8);
    char *buffer = (char *) malloc(FRAMES_PER_READ * frame_size);
    uint32_t total_bytes_recorded = 0;

    printf("Recording 5 seconds at %u Hz (%d channel)...\n", rate, CHANNELS);
    int total_loops = (rate * 5) / FRAMES_PER_READ;

    for (int i = 0; i < total_loops; i++) {
        snd_pcm_sframes_t frames_read = snd_pcm_readi(pcm_handle, buffer, FRAMES_PER_READ);

        if (frames_read < 0) {
            frames_read = snd_pcm_recover(pcm_handle, frames_read, 0);
            if (frames_read < 0) {
                fprintf(stderr, "Fatal ALSA read error: %s\n", snd_strerror(frames_read));
                break; // Exit loop immediately on unrecoverable error
            }
        }

        if (frames_read > 0) {
            size_t bytes_to_write = frames_read * frame_size;
            fwrite(buffer, 1, bytes_to_write, wav_file);
            total_bytes_recorded += bytes_to_write;
        }
    }

    // Immediately stop capture (snd_pcm_drop instead of snd_pcm_drain)
    snd_pcm_drop(pcm_handle);
    snd_pcm_close(pcm_handle);

    // Finalize WAV Header and close file safely
    write_wav_header(wav_file, total_bytes_recorded, rate);
    fclose(wav_file);
    free(buffer);

    printf("Saved %u bytes to output.wav successfully.\n", total_bytes_recorded);
    return 0;
}