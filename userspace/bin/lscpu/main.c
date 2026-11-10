/*
 * lscpu - Display information about the CPU architecture for akaOS
 *
 * Implements standard lscpu fields:
 * Architecture, CPU op-mode(s), Byte Order, Address sizes, CPU(s),
 * On-line CPU(s) list, Vendor ID, Model name, CPU family, Model, Stepping,
 * CPU MHz, BogoMIPS, Cache details (L1d, L1i, L2, L3), Flags
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char cpu_buf[8192];

static void print_usage(void) {
    puts("Usage: lscpu [options]\n"
         "Display information about the CPU architecture.\n\n"
         "Options:\n"
         "  -h, --help     display this help and exit\n"
         "  -V, --version  output version information and exit\n");
}

static int get_field(const char *buf, const char *key, char *out, size_t max_len) {
    if (!out || max_len == 0) return 0;
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = buf;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && (p[klen] == '\t' || p[klen] == ' ' || p[klen] == ':')) {
            const char *colon = strchr(p, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                size_t vi = 0;
                while (*colon && *colon != '\n' && vi + 1 < max_len) {
                    out[vi++] = *colon++;
                }
                out[vi] = '\0';
                return 1;
            }
        }
        const char *next = strchr(p, '\n');
        if (!next) break;
        p = next + 1;
    }
    return 0;
}

static char vendor[64];
static char model_name[128];
static char family[32];
static char model[32];
static char stepping[32];
static char mhz[32];
static char cache[64];
static char bogomips[32];
static char flags[1024];
static char addr_sizes[64];

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("lscpu from akaOS coreutils (XSCore)");
            return 0;
        }
    }

    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "lscpu: cannot open /proc/cpuinfo\n");
        return 1;
    }
    ssize_t n = read(fd, cpu_buf, sizeof(cpu_buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "lscpu: cannot read /proc/cpuinfo\n");
        return 1;
    }
    cpu_buf[n] = '\0';

    get_field(cpu_buf, "vendor_id", vendor, sizeof(vendor));
    get_field(cpu_buf, "model name", model_name, sizeof(model_name));
    get_field(cpu_buf, "cpu family", family, sizeof(family));
    get_field(cpu_buf, "model", model, sizeof(model));
    get_field(cpu_buf, "stepping", stepping, sizeof(stepping));
    get_field(cpu_buf, "cpu MHz", mhz, sizeof(mhz));
    get_field(cpu_buf, "cache size", cache, sizeof(cache));
    get_field(cpu_buf, "bogomips", bogomips, sizeof(bogomips));
    get_field(cpu_buf, "flags", flags, sizeof(flags));
    get_field(cpu_buf, "address sizes", addr_sizes, sizeof(addr_sizes));

    printf("Architecture:                    ");
#if defined(__x86_64__)
    puts("x86_64");
    puts("CPU op-mode(s):                  32-bit, 64-bit");
#elif defined(__i386__)
    puts("x86_32 (i686)");
    puts("CPU op-mode(s):                  32-bit");
#elif defined(__aarch64__)
    puts("aarch64");
    puts("CPU op-mode(s):                  64-bit");
#elif defined(__arm__)
    puts("arm32 (ARMv7-A)");
    puts("CPU op-mode(s):                  32-bit");
#elif defined(__riscv) && (__riscv_xlen == 64)
    puts("riscv64");
    puts("CPU op-mode(s):                  64-bit");
#elif defined(__riscv) && (__riscv_xlen == 32)
    puts("riscv32");
    puts("CPU op-mode(s):                  32-bit");
#else
    puts("generic");
#endif
    puts("Byte Order:                      Little Endian");
    if (addr_sizes[0]) {
        printf("Address sizes:                   %s\n", addr_sizes);
    } else {
        puts("Address sizes:                   48 bits physical, 48 bits virtual");
    }
    int cpu_count = 0;
    const char *scan = cpu_buf;
    while ((scan = strstr(scan, "processor\t:")) != NULL) {
        cpu_count++;
        scan += 11;
    }
    if (cpu_count == 0) cpu_count = 1;

    printf("CPU(s):                          %d\n", cpu_count);
    if (cpu_count > 1) {
        printf("On-line CPU(s) list:             0-%d\n", cpu_count - 1);
    } else {
        puts("On-line CPU(s) list:             0");
    }
    if (vendor[0]) printf("Vendor ID:                       %s\n", vendor);
    if (model_name[0]) printf("Model name:                      %s\n", model_name);
    if (family[0]) printf("CPU family:                      %s\n", family);
    if (model[0]) printf("Model:                           %s\n", model);
    puts("Thread(s) per core:              1");
    printf("Core(s) per socket:              %d\n", cpu_count);
    puts("Socket(s):                       1");
    if (stepping[0]) printf("Stepping:                        %s\n", stepping);
    if (mhz[0]) printf("CPU MHz:                         %s\n", mhz);
    if (bogomips[0]) printf("BogoMIPS:                        %s\n", bogomips);

    /* Caches */
    puts("L1d cache:                       32 KiB (8-way)");
    puts("L1i cache:                       32 KiB (8-way)");
    puts("L2 cache:                        512 KiB (8-way)");
    if (cache[0]) printf("L3 cache:                        %s\n", cache);
    puts("NUMA node(s):                    1");
    puts("NUMA node0 CPU(s):               0");
    if (flags[0]) printf("Flags:                           %s\n", flags);

    return 0;
}
