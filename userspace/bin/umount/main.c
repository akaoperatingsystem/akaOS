#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mount.h>

static void print_usage(void) {
    printf("Usage: umount [-f] <dir>\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *target = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            continue;
        }
        if (!target) {
            target = argv[i];
        }
    }

    if (!target) {
        print_usage();
        return 1;
    }

    if (umount(target) != 0) {
        fprintf(stderr, "umount: %s: %s\n", target, strerror(errno));
        return 1;
    }

    return 0;
}
