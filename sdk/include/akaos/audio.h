
/*
 * akaOS Audio Subsystem
 * OSS-compatible ABI header
 */
#ifndef AKAOS_AUDIO_H
#define AKAOS_AUDIO_H

#include <stdint.h>

#define SND_DEVICE_AUDIO "/dev/audio"
#define SND_DEVICE_DSP   "/dev/dsp"

typedef struct {
    uint32_t rate;
    uint16_t channels;
    uint16_t format;
} audio_params_t;

// IOCTL commands
#define SND_SET_PARAMS 0x1001
#define SND_GET_PARAMS 0x1002

#endif
