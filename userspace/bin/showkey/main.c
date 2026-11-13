
#include <stdio.h>
#include <unistd.h>
#include <akaos/input.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    printf("Press any key (ESC to exit)...\n");
    int fd = open("/dev/input/event0", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Cannot open keyboard device!\n");
        return 1;
    }
    
    while (1) {
        struct akaos_input_event ev;
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev)) break;
        if (ev.type == AKAOS_EV_KEY) {
            printf("Scancode: 0x%02x, Value: %d\n", ev.code, ev.value);
            if (ev.code == 0x01) break;
        }
    }
    close(fd);
    return 0;
}
