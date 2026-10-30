#include <akaos/display.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void usage(void) {
    puts("Usage: notify [--app ID] [--title TITLE] [--timeout MS] MESSAGE\n"
         "Post a desktop notification to the active Aurora session.");
}

int main(int argc, char **argv) {
    struct akaos_notification note;
    memset(&note, 0, sizeof(note));
    note.timeout_ms = 5000U;
    strcpy(note.app_id, "system");
    strcpy(note.title, "akaOS");
    const char *body = NULL;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[index], "--app") == 0 && index + 1 < argc) {
            strncpy(note.app_id, argv[++index], sizeof(note.app_id) - 1U);
        } else if (strcmp(argv[index], "--title") == 0 && index + 1 < argc) {
            strncpy(note.title, argv[++index], sizeof(note.title) - 1U);
        } else if (strcmp(argv[index], "--timeout") == 0 && index + 1 < argc) {
            int value = atoi(argv[++index]);
            if (value < 1000 || value > 60000) {
                fprintf(2, "notify: timeout must be between 1000 and 60000 ms\n");
                return 2;
            }
            note.timeout_ms = (uint32_t)value;
        } else if (argv[index][0] == '-') {
            fprintf(2, "notify: unknown option '%s'\n", argv[index]);
            return 2;
        } else if (!body) {
            body = argv[index];
        } else {
            fprintf(2, "notify: message must be one quoted argument\n");
            return 2;
        }
    }
    if (!body) {
        usage();
        return 2;
    }
    strncpy(note.body, body, sizeof(note.body) - 1U);
    int descriptor = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0);
    if (descriptor < 0) {
        fprintf(2, "notify: no active desktop session\n");
        return 1;
    }
    size_t size = (sizeof(struct akaos_compositor_channel) + 4095U) & ~(size_t)4095U;
    struct akaos_compositor_channel *channel =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    close(descriptor);
    if (channel == MAP_FAILED ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
        if (channel != MAP_FAILED) munmap(channel, size);
        fprintf(2, "notify: incompatible desktop session\n");
        return 1;
    }
    int submitted = akaos_notification_submit(channel, &note);
    munmap(channel, size);
    if (!submitted) {
        fprintf(2, "notify: desktop notification queue is full\n");
        return 1;
    }
    return 0;
}
