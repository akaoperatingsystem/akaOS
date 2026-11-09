#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "declare: shell builtin. Run 'declare [options] [name[=value]]' inside an interactive shell or script.\n");
    return 1;
}
