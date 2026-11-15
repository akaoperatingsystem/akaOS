#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define FTP_VERSION "1.0 (akaOS)"

static void print_usage(void) {
    printf("Usage: ftp [OPTION]... [HOST]\n"
           "File Transfer Protocol client.\n\n"
           "Options:\n"
           "  -v, --verbose       verbose mode (default)\n"
           "  -h, --help          display this help and exit\n");
}

int main(int argc, char **argv) {
    int i = 1;
    char *host = NULL;

    for (; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            // Placeholder for verbose toggle
        } else if (argv[i][0] != '-') {
            host = argv[i];
        } else {
            fprintf(stderr, "ftp: invalid option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!host) {
        printf("ftp> ");
        fflush(stdout);
        char cmd[256];
        if (fgets(cmd, sizeof(cmd), stdin)) {
            printf("Connecting to %s (simulated)...\n", cmd);
        }
    } else {
        printf("Connecting to %s...\n", host);
    }
    
    printf("FTP client implementation stub.\n");
    return 0;
}
