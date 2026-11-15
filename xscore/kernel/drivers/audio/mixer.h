
#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stdint.h>

#define MAX_CHANNELS 8
#define MIXER_BUFFER_SIZE 4096

typedef struct {
    int16_t *buffer;
    uint32_t length;
    uint32_t write_ptr;
    uint32_t read_ptr;
} pcm_stream_t;

void mixer_init(void);
void mixer_process(pcm_stream_t *streams, uint32_t stream_count, int16_t *output);

#endif
