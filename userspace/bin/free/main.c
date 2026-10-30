#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char mem_buf[1024];

static unsigned long parse_kb(const char *buf, const char *key) {
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    return (unsigned long)strtoul(p, NULL, 10);
}

static void format_human(char *out, size_t out_sz, unsigned long kb) {
    if (kb == 0) {
        snprintf(out, out_sz, "0B");
        return;
    }
    if (kb >= 1024 * 1024) {
        snprintf(out, out_sz, "%luG", kb / (1024 * 1024));
    } else if (kb >= 1024) {
        snprintf(out, out_sz, "%luM", kb / 1024);
    } else {
        snprintf(out, out_sz, "%luK", kb);
    }
}

int main(int argc, char **argv) {
    enum { UNIT_KB, UNIT_BYTES, UNIT_MB, UNIT_GB, UNIT_HUMAN } unit = UNIT_KB;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bytes") == 0) {
            unit = UNIT_BYTES;
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--kibi") == 0) {
            unit = UNIT_KB;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mebi") == 0) {
            unit = UNIT_MB;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gibi") == 0) {
            unit = UNIT_GB;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human") == 0) {
            unit = UNIT_HUMAN;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: free [options]\n"
                   "Display amount of free and used memory in the system.\n\n"
                   "Options:\n"
                   "  -b, --bytes         Show output in bytes\n"
                   "  -k, --kibi          Show output in kibibytes (default)\n"
                   "  -m, --mebi          Show output in mebibytes\n"
                   "  -g, --gibi          Show output in gibibytes\n"
                   "  -h, --human         Show human-readable output\n"
                   "      --help          Display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "free: unrecognized option '%s'\n", argv[i]);
            return 1;
        }
    }

    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "free: cannot open /proc/meminfo\n");
        return 1;
    }
    ssize_t n = read(fd, mem_buf, sizeof(mem_buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "free: cannot read /proc/meminfo\n");
        return 1;
    }
    mem_buf[n] = '\0';

    unsigned long total = parse_kb(mem_buf, "MemTotal");
    unsigned long free_mem = parse_kb(mem_buf, "MemFree");
    unsigned long available = parse_kb(mem_buf, "MemAvailable");
    unsigned long buffers = parse_kb(mem_buf, "Buffers");
    unsigned long cached = parse_kb(mem_buf, "Cached");
    unsigned long swap_total = parse_kb(mem_buf, "SwapTotal");
    unsigned long swap_free = parse_kb(mem_buf, "SwapFree");

    unsigned long buff_cache = buffers + cached;
    unsigned long used = (total >= free_mem + buff_cache) ? (total - free_mem - buff_cache) : 0;
    unsigned long swap_used = (swap_total >= swap_free) ? (swap_total - swap_free) : 0;
    if (available == 0) available = free_mem;

    if (unit == UNIT_HUMAN) {
        char s_tot[16], s_usd[16], s_fre[16], s_buf[16], s_avl[16];
        char sw_tot[16], sw_usd[16], sw_fre[16];
        format_human(s_tot, sizeof(s_tot), total);
        format_human(s_usd, sizeof(s_usd), used);
        format_human(s_fre, sizeof(s_fre), free_mem);
        format_human(s_buf, sizeof(s_buf), buff_cache);
        format_human(s_avl, sizeof(s_avl), available);
        format_human(sw_tot, sizeof(sw_tot), swap_total);
        format_human(sw_usd, sizeof(sw_usd), swap_used);
        format_human(sw_fre, sizeof(sw_fre), swap_free);

        printf("               total        used        free      shared  buff/cache   available\n");
        printf("Mem:   %12s%12s%12s%12s%12s%12s\n", s_tot, s_usd, s_fre, "0B", s_buf, s_avl);
        printf("Swap:  %12s%12s%12s\n", sw_tot, sw_usd, sw_fre);
    } else {
        unsigned long factor_num = 1;
        unsigned long factor_den = 1;
        if (unit == UNIT_BYTES) {
            factor_num = 1024;
            factor_den = 1;
        } else if (unit == UNIT_MB) {
            factor_num = 1;
            factor_den = 1024;
        } else if (unit == UNIT_GB) {
            factor_num = 1;
            factor_den = 1024 * 1024;
        }

        unsigned long d_tot = (total * factor_num) / factor_den;
        unsigned long d_usd = (used * factor_num) / factor_den;
        unsigned long d_fre = (free_mem * factor_num) / factor_den;
        unsigned long d_buf = (buff_cache * factor_num) / factor_den;
        unsigned long d_avl = (available * factor_num) / factor_den;
        unsigned long d_sw_tot = (swap_total * factor_num) / factor_den;
        unsigned long d_sw_usd = (swap_used * factor_num) / factor_den;
        unsigned long d_sw_fre = (swap_free * factor_num) / factor_den;

        printf("               total        used        free      shared  buff/cache   available\n");
        printf("Mem:   %12lu%12lu%12lu%12lu%12lu%12lu\n",
               d_tot, d_usd, d_fre, 0UL, d_buf, d_avl);
        printf("Swap:  %12lu%12lu%12lu\n",
               d_sw_tot, d_sw_usd, d_sw_fre);
    }

    return 0;
}
