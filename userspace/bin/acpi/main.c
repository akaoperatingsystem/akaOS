#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char acpi_buf[4096];

static void print_file(const char *path, const char *title) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "acpi: cannot open %s\n", path);
        return;
    }
    ssize_t n = read(fd, acpi_buf, sizeof(acpi_buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "acpi: no data available from %s\n", path);
        return;
    }
    acpi_buf[n] = '\0';
    if (title) {
        puts(title);
    }
    printf("%s", acpi_buf);
}

static void print_usage(void) {
    puts("Usage: acpi [options]\n"
         "Display Advanced Configuration and Power Interface (ACPI) telemetry.\n\n"
         "Options:\n"
         "  -c, --cpu      display CPU and interrupt controller topology (MADT)\n"
         "  -p, --power    display hardware power management and sleep states (FADT)\n"
         "  -v, --verbose  display complete ACPI table overview, FADT, and MADT\n"
         "  -h, --help     display this help and exit\n"
         "  -V, --version  output version information and exit\n");
}

int main(int argc, char **argv) {
    int show_cpu = 0;
    int show_power = 0;
    int show_verbose = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("acpi (akaOS XSCore) 1.0");
            return 0;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--cpu") == 0) {
            show_cpu = 1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--power") == 0) {
            show_power = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            show_verbose = 1;
        } else {
            fprintf(stderr, "acpi: unrecognized option '%s'\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (show_verbose) {
        print_file("/proc/acpi/info", "=== ACPI Subsystem Overview ===");
        print_file("/proc/acpi/fadt", "\n=== ACPI Power Management (FADT) ===");
        print_file("/proc/acpi/madt", "\n=== ACPI Interrupt & Processor Topology (MADT) ===");
        return 0;
    }

    if (show_cpu) {
        print_file("/proc/acpi/madt", "=== ACPI Interrupt & Processor Topology (MADT) ===");
        return 0;
    }

    if (show_power) {
        print_file("/proc/acpi/fadt", "=== ACPI Power Management (FADT) ===");
        return 0;
    }

    /* Default: overview */
    print_file("/proc/acpi/info", "=== ACPI Subsystem Overview ===");
    return 0;
}
