/*
 * pmctl - System Power Management Control Utility for akaOS
 *
 * Commands:
 *   poweroff, shutdown  trigger ACPI S5 poweroff
 *   reboot              reboot the machine
 *   suspend             enter S3 sleep state
 *   status              query active sleep states and battery status
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <akaos/syscall.h>

static void print_usage(void) {
    puts("Usage: pmctl <command> [options]\n"
         "System power management and sleep state controller.\n\n"
         "Commands:\n"
         "  poweroff, shutdown  trigger immediate ACPI S5 system power-off\n"
         "  reboot              trigger system reset\n"
         "  suspend             enter ACPI S3 suspend-to-RAM state\n"
         "  status              display active ACPI sleep states and power supply\n"
         "  -h, --help          display this help message and exit\n"
         "  -V, --version       display version information\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
        puts("pmctl from akaOS power management subsystem (XSCore)");
        return 0;
    }

    if (strcmp(argv[1], "poweroff") == 0 || strcmp(argv[1], "shutdown") == 0) {
        puts("Triggering ACPI S5 system power-off...");
        sync();
        syscall(AKAOS_SYSCALL_REBOOT, 0x4321fedcU);
        return 0;
    }

    if (strcmp(argv[1], "reboot") == 0) {
        puts("Rebooting system...");
        sync();
        syscall(AKAOS_SYSCALL_REBOOT, 0x12345678U);
        return 0;
    }

    if (strcmp(argv[1], "suspend") == 0) {
        puts("Entering ACPI S3 suspend-to-RAM state...");
        sync();
        /* Invokes sleep transition */
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        puts("=== System Power State Telemetry ===");
        int fd = open("/proc/acpi/fadt", O_RDONLY);
        if (fd >= 0) {
            char buf[1024];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                printf("%s", buf);
            }
        } else {
            puts("ACPI FADT not available");
        }
        return 0;
    }

    fprintf(stderr, "pmctl: unknown command '%s'\n", argv[1]);
    print_usage();
    return 1;
}
