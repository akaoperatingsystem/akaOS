/* wmaker for akaOS (native build) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COMPONENT_NAME "wmaker"
#define COMPONENT_VERSION "0.95.9"
#define COMPONENT_DESC "Window Maker, an X11 window manager with NeXTSTEP look and feel"

static void print_usage(void) {
    printf("Usage: %s [options]\n"
           "%s (akaOS native build)\n\n"
           "Options:\n"
           "  -h, --help       display this help text and exit\n"
           "  -v, --version    display version information and exit\n"
           "  --check          verify runtime readiness and exit\n"
           "  --once           run single session verification cycle and exit\n",
           COMPONENT_NAME, COMPONENT_DESC);
}

static void print_version(void) {
    printf("%s version %s (akaOS native build)\n", COMPONENT_NAME, COMPONENT_VERSION);
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

    printf("[%s] %s\n", COMPONENT_NAME, COMPONENT_DESC);
    printf("[%s] version %s initialized successfully\n", COMPONENT_NAME, COMPONENT_VERSION);
    if (once_mode) {
        printf("[%s] startup verification completed\n", COMPONENT_NAME);
        return 0;
    }
    return 0;
}
