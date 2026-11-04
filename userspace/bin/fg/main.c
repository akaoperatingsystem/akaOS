#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "fg: shell builtin. Run 'bg' inside an interactive shell (akash or bash).\n");
    return 1;
}
