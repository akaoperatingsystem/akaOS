#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void print_usage(void) {
    printf("Usage: chroot NEWROOT [COMMAND [ARGS]...]\n"
           "       chroot OPTION\n\n"
           "Run a command with a different root directory.\n\n"
           "  --help        display this help and exit\n"
           "  --version     output version information and exit\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    } else if (strcmp(argv[1], "--version") == 0) {
        printf("chroot (akaOS) 0.1\n");
        return 0;
    }

    if (chroot(argv[1]) != 0) {
        perror("chroot");
        return 1;
    }

    if (chdir("/") != 0) {
        perror("chdir");
        return 1;
    }

    if (argc == 2) {
        char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/sh";
        char *args[] = {shell, "-i", NULL};
        execvp(shell, args);
        perror("execvp");
        return 1;
    } else {
        execvp(argv[2], &argv[2]);
        perror("execvp");
        return 1;
    }
}
