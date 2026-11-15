
#include "mixer.h"
#include <stdint.h>

void mixer_init(void) {
    // Mixer initialization
}

void mixer_process(pcm_stream_t *streams, uint32_t stream_count, int16_t *output) {
    // Zero out output
    for(int i = 0; i < MIXER_BUFFER_SIZE; i++) output[i] = 0;

    // Simple sum and clamp
    for(uint32_t s = 0; s < stream_count; s++) {
        for(int i = 0; i < MIXER_BUFFER_SIZE; i++) {
            int32_t sample = output[i] + streams[s].buffer[i];
            
            // Simple hard clamp for now
            if(sample > 32767) sample = 32767;
            if(sample < -32768) sample = -32768;
            
            output[i] = (int16_t)sample;
        }
    }
}
