/*
 * battery - Battery and Power Supply Telemetry Monitor for akaOS
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        puts("Usage: battery\nDisplay battery charge, charging state, and power supply telemetry.");
        return 0;
    }
    if (argc >= 2 && (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0)) {
        puts("battery (akaOS XSCore) 1.0");
        return 0;
    }

    int fd = open("/proc/acpi/battery", O_RDONLY);
    if (fd < 0) {
        puts("Battery 0: Full, 100%, AC connected");
        return 0;
    }

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        puts("Battery 0: Full, 100%, AC connected");
        return 0;
    }
    buf[n] = '\0';

    int pct = 100;
    const char *p = strstr(buf, "percentage:              ");
    if (p) pct = atoi(p + 25);

    int ac = 1;
    p = strstr(buf, "ac online:               ");
    if (p && strncmp(p + 25, "no", 2) == 0) ac = 0;

    printf("Battery 0: %s, %d%%, %s\n", ac ? "Full" : "Discharging", pct, ac ? "AC online" : "Battery power");
    return 0;
}
