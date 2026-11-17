#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "Usage: hostname [name]\n");
        return 1;
    }

    if (argc == 2) {
        if (sethostname(argv[1], strlen(argv[1])) != 0) {
            perror("hostname");
            return 1;
        }
        return 0;
    } else {
        char name[64];
        if (gethostname(name, sizeof(name)) == 0) {
            printf("%s\n", name);
            return 0;
        } else {
            perror("hostname");
            return 1;
        }
    }
}
