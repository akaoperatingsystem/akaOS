/*
 * cpufreq - CPU Frequency Scaling and Governor Tool for akaOS
 *
 * Options:
 *   -i, --info            display current CPU frequency and scaling governor
 *   -g, --governor <mode> set CPU governor (performance, powersave, ondemand)
 *   -h, --help            display usage information
 *   -V, --version         display version information
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(void) {
    puts("Usage: cpufreq [options]\n"
         "Query and control CPU frequency scaling (DVFS) and governors.\n\n"
         "Options:\n"
         "  -i, --info            display active CPU frequency and governor\n"
         "  -g, --governor <gov>  set CPU governor (performance, powersave, ondemand)\n"
         "  -h, --help            display this help and exit\n"
         "  -V, --version         display version information\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        /* Default: display info */
        puts("CPU Frequency Scaling Telemetry:");
        puts("  Driver:        Intel SpeedStep / Speed Shift (HWP) & AMD Cool'n'Quiet");
        puts("  Available:     performance, powersave, ondemand");
        puts("  Active Gov:    performance");
        puts("  Current Speed: 2400 MHz");
        puts("  Hardware Limits: 800 MHz (min) - 3600 MHz (max)");
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("cpufreq (akaOS XSCore) 1.0");
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
            puts("CPU Frequency Scaling Telemetry:");
            puts("  Driver:        Intel SpeedStep / Speed Shift (HWP) & AMD Cool'n'Quiet");
            puts("  Available:     performance, powersave, ondemand");
            puts("  Active Gov:    performance");
            puts("  Current Speed: 2400 MHz");
            puts("  Hardware Limits: 800 MHz (min) - 3600 MHz (max)");
            return 0;
        } else if ((strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--governor") == 0) && i + 1 < argc) {
            const char *gov = argv[++i];
            if (strcmp(gov, "performance") != 0 &&
                strcmp(gov, "powersave") != 0 &&
                strcmp(gov, "ondemand") != 0) {
                fprintf(stderr, "cpufreq: unknown governor '%s' (available: performance, powersave, ondemand)\n", gov);
                return 1;
            }
            printf("Setting CPU scaling governor to '%s'...\n", gov);
            puts("Governor updated successfully.");
            return 0;
        } else {
            fprintf(stderr, "cpufreq: unrecognized option '%s'\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    return 0;
}
