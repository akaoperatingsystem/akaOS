#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "break: shell builtin. Run 'break' inside a loop in an interactive shell or script.\n");
    return 1;
}
