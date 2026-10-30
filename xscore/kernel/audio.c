#include <xscore/audio.h>
#include <xscore/serial.h>
#include <xscore/thread.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t audio_ring[XSCORE_AUDIO_BUFFER_SIZE];
static uint32_t audio_head = 0;
static uint32_t audio_tail = 0;
static uint32_t audio_bytes_written = 0;

#if defined(__x86_64__) || defined(__i386__)
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
#endif

void xscore_audio_initialize(void) {
    audio_head = 0;
    audio_tail = 0;
    audio_bytes_written = 0;
    xscore_audio_stop();
}

void xscore_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0) {
        xscore_audio_stop();
        return;
    }

#if defined(__x86_64__) || defined(__i386__)
    uint32_t divisor = 1193180U / freq_hz;
    if (divisor > 65535U) divisor = 65535U;
    if (divisor < 1) divisor = 1;

    outb(0x43, 0xb6);
    outb(0x42, (uint8_t)(divisor & 0xff));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xff));

    uint8_t tmp = inb(0x61);
    if ((tmp & 3) != 3) {
        outb(0x61, tmp | 3);
    }
#endif

    if (duration_ms > 0) {
        /* Busy/yield loop for tone duration */
        for (uint32_t i = 0; i < duration_ms / 10; ++i) {
            xscore_thread_yield();
        }
        xscore_audio_stop();
    }
}

void xscore_audio_stop(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint8_t tmp = inb(0x61) & 0xfc;
    outb(0x61, tmp);
#endif
}

int64_t xscore_audio_write(const void *buf, uint64_t count) {
    if (!buf || count == 0) return 0;
    const uint8_t *src = buf;
    uint64_t written = 0;

    for (uint64_t i = 0; i < count; ++i) {
        uint32_t next = (audio_head + 1) % XSCORE_AUDIO_BUFFER_SIZE;
        if (next == audio_tail) break; /* Buffer full */
        audio_ring[audio_head] = src[i];
        audio_head = next;
        written++;
        audio_bytes_written++;
    }

    /* If written raw PCM square wave samples or tone headers, activate speaker */
    if (written > 0) {
        uint32_t sample = (uint32_t)src[0];
        uint32_t freq = 440 + (sample * 4);
        xscore_audio_play_tone(freq, 0);
    }

    return (int64_t)written;
}

int64_t xscore_audio_read(void *buf, uint64_t count) {
    if (!buf || count == 0) return 0;
    uint8_t *dst = buf;
    uint64_t read_bytes = 0;

    while (audio_tail != audio_head && read_bytes < count) {
        dst[read_bytes++] = audio_ring[audio_tail];
        audio_tail = (audio_tail + 1) % XSCORE_AUDIO_BUFFER_SIZE;
    }

    return (int64_t)read_bytes;
}

int xscore_audio_poll_write_ready(void) {
    uint32_t next = (audio_head + 1) % XSCORE_AUDIO_BUFFER_SIZE;
    return next != audio_tail;
}
