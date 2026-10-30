#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: tty\nPrint the terminal connected to standard input.");
        return 0;
    }
    if (argc != 1) {
        puts("tty: extra operand");
        return 2;
    }
    char *name = ttyname(STDIN_FILENO);
    if (!name) {
        puts("not a tty");
        return 1;
    }
    puts(name);
    return 0;
}
