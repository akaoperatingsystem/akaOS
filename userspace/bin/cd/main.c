#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "cd: shell builtin. Run 'cd' inside an interactive shell or script to change directories.\n");
    return 1;
}
