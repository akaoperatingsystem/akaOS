#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "dirs: shell builtin. Run 'dirs' inside an interactive shell (akash or bash).
");
    return 1;
}
