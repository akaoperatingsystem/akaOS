#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "builtin: shell builtin. Run 'builtin <cmd>' inside an interactive shell or script.\n");
    return 1;
}
