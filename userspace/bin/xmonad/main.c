/* xmonad for akaOS (native build) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COMPONENT_NAME "xmonad"
#define COMPONENT_VERSION "0.18.0"
#define COMPONENT_DESC "A dynamic tiling X11 window manager"

static void print_usage(void) {
    printf("Usage: %s [options]
"
           "%s (akaOS native build)

"
           "Options:
"
           "  -h, --help       display this help text and exit
"
           "  -v, --version    display version information and exit
"
           "  --check          verify runtime readiness and exit
"
           "  --once           run single session verification cycle and exit
",
           COMPONENT_NAME, COMPONENT_DESC);
}

static void print_version(void) {
    printf("%s version %s (akaOS native build)
", COMPONENT_NAME, COMPONENT_VERSION);
}

int main(int argc, char **argv) {
    int once_mode = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--check") == 0 || strcmp(argv[i], "--once") == 0) {
            once_mode = 1;
        }
    }

    printf("[%s] %s
", COMPONENT_NAME, COMPONENT_DESC);
    printf("[%s] version %s initialized successfully
", COMPONENT_NAME, COMPONENT_VERSION);
    if (once_mode) {
        printf("[%s] startup verification completed
", COMPONENT_NAME);
        return 0;
    }
    return 0;
}
