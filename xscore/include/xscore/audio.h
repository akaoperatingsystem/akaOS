#ifndef XSCORE_AUDIO_H
#define XSCORE_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#define XSCORE_AUDIO_BUFFER_SIZE 4096

void xscore_audio_initialize(void);
void xscore_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms);
void xscore_audio_stop(void);
int64_t xscore_audio_write(const void *buf, uint64_t count);
int64_t xscore_audio_read(void *buf, uint64_t count);
int xscore_audio_poll_write_ready(void);

#endif
