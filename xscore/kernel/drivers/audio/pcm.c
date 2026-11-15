
#include "pcm.h"
#include <xscore/memory.h>

void pcm_buffer_init(pcm_buffer_t *buf, uint32_t size) {
    buf->buffer = kmalloc(size);
    buf->size = size;
    buf->head = 0;
    buf->tail = 0;
}

uint32_t pcm_buffer_push(pcm_buffer_t *buf, uint8_t *data, uint32_t len) {
    // Basic ring buffer push
    for(uint32_t i = 0; i < len; i++) {
        buf->buffer[buf->head] = data[i];
        buf->head = (buf->head + 1) % buf->size;
    }
    return len;
}

uint32_t pcm_buffer_pop(pcm_buffer_t *buf, uint8_t *data, uint32_t len) {
    // Basic ring buffer pop
    for(uint32_t i = 0; i < len; i++) {
        data[i] = buf->buffer[buf->tail];
        buf->tail = (buf->tail + 1) % buf->size;
    }
    return len;
}
