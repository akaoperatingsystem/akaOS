#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "case: shell reserved word. Use 'case WORD in PATTERNS) COMMANDS ;; ... esac' in shell scripts.\n");
    return 1;
}
