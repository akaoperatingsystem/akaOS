
#ifndef PCM_H
#define PCM_H
#include <stdint.h>

typedef struct {
    uint8_t *buffer;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
} pcm_buffer_t;

void pcm_buffer_init(pcm_buffer_t *buf, uint32_t size);
uint32_t pcm_buffer_push(pcm_buffer_t *buf, uint8_t *data, uint32_t len);
uint32_t pcm_buffer_pop(pcm_buffer_t *buf, uint8_t *data, uint32_t len);

#endif
