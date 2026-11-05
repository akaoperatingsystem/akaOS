#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "caller: shell builtin. Run 'caller' inside a shell function or sourced script.\n");
    return 1;
}
