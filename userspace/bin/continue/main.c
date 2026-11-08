#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "continue: shell builtin. Run 'continue [n]' inside a loop in an interactive shell or script.\n");
    return 1;
}
