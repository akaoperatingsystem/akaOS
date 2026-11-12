#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>

#ifdef __AKAOS__
#include <akaos/fs.h>
#define STAT_STRUCT akaos_stat
#define STAT_MODE st_mode
#define STAT_SIZE st_size
#else
#include <sys/types.h>
#define akaos_dirent dirent
#define STAT_STRUCT stat
#define STAT_MODE st_mode
#define STAT_SIZE st_size
#endif

#define MAX_DEPTH 16
static struct akaos_dirent entries_pool[MAX_DEPTH][16] __attribute__((unused));
static char child_paths[MAX_DEPTH][256];

static void print_usage(void) {
    printf("Usage: du [OPTION]... [FILE]...\n");
    printf("Summarize device usage of the set of FILEs, recursively for directories.\n\n");
    printf("Options:\n");
    printf("  -a, --all             write counts for all files, not just directories\n");
    printf("  -c, --total           produce a grand total\n");
    printf("  -d, --max-depth=N     print the total for a directory only if it is N or fewer\n");
    printf("                        levels below the command line argument\n");
    printf("  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n");
    printf("  -s, --summarize       display only a total for each argument\n");
    printf("  -help, --help         display this help and exit\n");
}

static void print_size(uint64_t kb, int human) {
    if (!human) {
        printf("%llu\t", (unsigned long long)kb);
        return;
    }

    uint64_t bytes = kb * 1024ULL;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        uint64_t gb = bytes / (1024ULL * 1024ULL * 1024ULL);
        uint64_t frac = (bytes % (1024ULL * 1024ULL * 1024ULL)) / (1024ULL * 1024ULL * 100ULL);
        if (gb < 10) printf("%llu.%lluG\t", (unsigned long long)gb, (unsigned long long)frac);
        else printf("%lluG\t", (unsigned long long)gb);
    } else if (bytes >= 1024ULL * 1024ULL) {
        uint64_t mb = bytes / (1024ULL * 1024ULL);
        uint64_t frac = (bytes % (1024ULL * 1024ULL)) / (1024ULL * 100ULL);
        if (mb < 10) printf("%llu.%lluM\t", (unsigned long long)mb, (unsigned long long)frac);
        else printf("%lluM\t", (unsigned long long)mb);
    } else if (bytes >= 1024ULL) {
        uint64_t kb_val = (unsigned long long)kb;
        printf("%lluK\t", (unsigned long long)kb_val);
    } else {
        printf("%lluB\t", (unsigned long long)bytes);
    }
}

static uint64_t du_path(const char *path, int depth, int max_depth, int opt_all, int opt_human) {
    struct STAT_STRUCT st;
    if (stat(path, (struct stat *)&st) != 0) {
        fprintf(stderr, "du: cannot access '%s': %s\n", path, strerror(errno));
        return 0;
    }

    if (!S_ISDIR(st.STAT_MODE)) {
        uint64_t kb = (st.STAT_SIZE + 1023ULL) / 1024ULL;
        if (kb == 0 && st.STAT_SIZE > 0) kb = 1;
        if (opt_all && (max_depth < 0 || depth <= max_depth)) {
            print_size(kb, opt_human);
            printf("%s\n", path);
        }
        return kb;
    }

    uint64_t dir_kb = 1;
    if (depth < MAX_DEPTH) {
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            #ifdef __AKAOS__
            struct akaos_dirent *entries = entries_pool[depth];
            for (;;) {
                ssize_t count = getdents(fd, entries, 16);
                if (count <= 0) break;
                for (ssize_t i = 0; i < count; ++i) {
                    const char *name = entries[i].name;
            #else
            DIR *d = fdopendir(fd);
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                const char *name = entry->d_name;
            #endif
                    if (!name || !*name) continue;
                    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

                    char *child = child_paths[depth];
                    if (strcmp(path, "/") == 0) {
                        snprintf(child, sizeof(child_paths[0]), "/%s", name);
                    } else {
                        snprintf(child, sizeof(child_paths[0]), "%s/%s", path, name);
                    }

                    dir_kb += du_path(child, depth + 1, max_depth, opt_all, opt_human);
                }
            #ifdef __AKAOS__
            }
            #else
            closedir(d);
            #endif
            close(fd);
        }
    }

    if (max_depth < 0 || depth <= max_depth) {
        print_size(dir_kb, opt_human);
        printf("%s\n", path);
    }

    return dir_kb;
}

int main(int argc, char *argv[]) {
    int opt_all = 0;
    int opt_total = 0;
    int opt_human = 0;
    int max_depth = -1;
    int first_arg = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human-readable") == 0) {
            opt_human = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            opt_all = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--summarize") == 0) {
            max_depth = 0;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--total") == 0) {
            opt_total = 1;
        } else if (strncmp(argv[i], "-d", 2) == 0) {
            if (strlen(argv[i]) > 2) {
                max_depth = atoi(argv[i] + 2);
            } else if (i + 1 < argc) {
                max_depth = atoi(argv[i + 1]);
                i++;
            }
        } else if (strncmp(argv[i], "--max-depth=", 12) == 0) {
            max_depth = atoi(argv[i] + 12);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "--") == 0) {
            first_arg = i + 1;
            break;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "du: invalid option -- '%s'\n", argv[i]);
            print_usage();
            return 1;
        } else {
            first_arg = i;
            break;
        }
    }

    uint64_t grand_total = 0;

    if (first_arg == -1 || first_arg >= argc) {
        grand_total += du_path(".", 0, max_depth, opt_all, opt_human);
    } else {
        for (int i = first_arg; i < argc; i++) {
            grand_total += du_path(argv[i], 0, max_depth, opt_all, opt_human);
        }
    }

    if (opt_total) {
        print_size(grand_total, opt_human);
        printf("total\n");
    }

    return 0;
}
