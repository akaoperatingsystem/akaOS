#include <sys/reboot.h>
#include <unistd.h>
#include <string.h>

static int write_str(int fd, const char *str) {
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        write_str(STDOUT_FILENO, "Usage: poweroff\nPower off the system and shut down hardware.\n");
        return 0;
    }
    if (argc != 1) {
        write_str(STDERR_FILENO, "poweroff: invalid option or extra operand\n");
        return 1;
    }

    reboot(RB_POWER_OFF);
    write_str(STDERR_FILENO, "poweroff: error initiating poweroff\n");
    return 1;
}
