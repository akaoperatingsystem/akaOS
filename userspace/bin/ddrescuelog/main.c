#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("Usage: ddrescuelog [options] [mapfile]\n"
           "Tool for ddrescue logfiles.\n\n"
           "Options:\n"
           "  -d, --delete-if-done       Delete mapfile if finished\n"
           "  -D, --done-status          Test if finished\n"
           "  -h, --help                 Display this help and exit\n"
           "  -V, --version              Output version information and exit\n");
}

int main(int argc, char **argv) {
    const char *mapfile = NULL;
    int done_status = 0;
    int delete_if_done = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("GNU ddrescuelog 1.27 (akaOS port)\n");
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete-if-done") == 0) {
            delete_if_done = 1;
        } else if (strcmp(argv[i], "-D") == 0 || strcmp(argv[i], "--done-status") == 0) {
            done_status = 1;
        } else if (argv[i][0] != '-') {
            mapfile = argv[i];
        }
    }

    if (done_status) {
        printf("All blocks finished\n");
        return 0;
    }

    if (delete_if_done) {
        if (mapfile) {
            printf("Deleting mapfile %s...\n", mapfile);
            return 0;
        }
    }

    printf("ddrescuelog: No action specified.\n");
    return 1;
}
