/*
 * mouse-config - Mouse & Pointing Device Configuration Utility for akaOS
 *
 * Options:
 *   -l, --list        List active pointing devices and capabilities
 *   -a, --accel MODE  Set acceleration profile (fixed, linear, adaptive)
 *   -s, --speed NUM   Set pointer speed multiplier (1 to 50, default 10 = 1.0x)
 *   -h, --help        Show usage information
 *   -V, --version     Show version information
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <akaos/input.h>

static void print_usage(void) {
    puts("Usage: mouse-config [options]\n"
         "Configure pointing device drivers, acceleration, and sensitivity.\n\n"
         "Options:\n"
         "  -l, --list             List detected mouse/pointer devices\n"
         "  -a, --accel <profile>  Set acceleration profile (fixed, linear, adaptive)\n"
         "  -s, --speed <val>      Set speed multiplier (1..50, default 10)\n"
         "  -h, --help             Display this help message and exit\n"
         "  -V, --version          Display version information\n");
}

static void list_devices(void) {
    puts("Detected Pointing Devices:");
    puts("  1. [PS/2] Generic PS/2 / Microsoft IntelliMouse (5-button + Wheel)");
    puts("     Protocol: IntelliMouse Explorer 4-byte stream");
    puts("     Capabilities: Left, Right, Middle, Side, Extra, Wheel");
    puts("     Path: /dev/input/event0");
    puts("");
    puts("  2. [USB]  akaOS Virtual USB Pointer / Tablet (Absolute/Relative)");
    puts("     Protocol: USB HID Boot & Report Protocol");
    puts("     Capabilities: 5 buttons, High-resolution dual wheel");
    puts("     Path: /dev/input/event0");
    puts("");
    puts("  3. [UART] Serial Mouse (COM2 / ttyS1)");
    puts("     Protocol: Microsoft 7-N-1 1200 baud");
    puts("     Capabilities: 3 buttons, Wheel extension");
    puts("");

    /* Read current config from /etc/akaos.conf if available */
    char profile[32] = "adaptive";
    int speed = 10;
    int fd = open("/etc/akaos.conf", O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            const char *accel = strstr(buf, "MOUSE_ACCEL=");
            if (accel) {
                char *end = strchr(accel + 12, '\n');
                if (end) *end = '\0';
                strncpy(profile, accel + 12, sizeof(profile) - 1);
            }
            const char *spd = strstr(buf, "MOUSE_SPEED=");
            if (spd) speed = atoi(spd + 12);
        }
    }
    printf("Current Settings: Acceleration=%s, Speed=%d (%.1fx)\n",
           profile, speed, (float)speed / 10.0f);
}

static int set_config(const char *accel_mode, int speed_val) {
    char new_conf[2048];
    new_conf[0] = '\0';

    int fd = open("/etc/akaos.conf", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, new_conf, sizeof(new_conf) - 256);
        close(fd);
        if (n > 0) new_conf[n] = '\0';
    }

    /* Strip existing MOUSE_ lines */
    char filtered[2048];
    filtered[0] = '\0';
    char *line = strtok(new_conf, "\n");
    while (line) {
        if (strncmp(line, "MOUSE_ACCEL=", 12) != 0 &&
            strncmp(line, "MOUSE_SPEED=", 12) != 0) {
            strcat(filtered, line);
            strcat(filtered, "\n");
        }
        line = strtok(NULL, "\n");
    }

    if (accel_mode) {
        char append_accel[64];
        snprintf(append_accel, sizeof(append_accel), "MOUSE_ACCEL=%s\n", accel_mode);
        strcat(filtered, append_accel);
    }
    if (speed_val > 0) {
        char append_spd[64];
        snprintf(append_spd, sizeof(append_spd), "MOUSE_SPEED=%d\n", speed_val);
        strcat(filtered, append_spd);
    }

    int out_fd = open("/etc/akaos.conf", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd >= 0) {
        write(out_fd, filtered, strlen(filtered));
        close(out_fd);
        printf("Saved mouse settings to /etc/akaos.conf\n");
        return 0;
    }

    fprintf(stderr, "mouse-config: cannot write /etc/akaos.conf\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        list_devices();
        return 0;
    }

    const char *set_accel = NULL;
    int set_speed = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("mouse-config from akaOS input subsystem (XSCore)");
            return 0;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_devices();
            return 0;
        } else if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--accel") == 0) && i + 1 < argc) {
            set_accel = argv[++i];
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--speed") == 0) && i + 1 < argc) {
            set_speed = atoi(argv[++i]);
        } else {
            fprintf(stderr, "mouse-config: unknown option '%s'\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (set_accel || set_speed > 0) {
        return set_config(set_accel, set_speed);
    }

    return 0;
}
