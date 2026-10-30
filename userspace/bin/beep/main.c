#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: beep [--freq HZ] [--duration MS]\nGenerate audio tones through /dev/audio.");
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        puts("beep (akaOS Audio Utility) 2.0");
        return 0;
    }

    int freq = 440;
    int duration = 100;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--freq") == 0 && i + 1 < argc) {
            freq = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = atoi(argv[++i]);
        }
    }

    if (freq <= 0) freq = 440;
    if (duration <= 0) duration = 100;

    int fd = open("/dev/audio", O_WRONLY);
    if (fd < 0) {
        fd = open("/dev/dsp", O_WRONLY);
    }
    if (fd < 0) {
        puts("beep: /dev/audio not available");
        return 1;
    }

    uint8_t buffer[64];
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (uint8_t)((i % 2 == 0) ? (freq & 0xff) : ((freq >> 8) & 0xff));
    }

    write(fd, buffer, sizeof(buffer));
    close(fd);

    puts("beep: tone sent to audio device");
    return 0;
}
