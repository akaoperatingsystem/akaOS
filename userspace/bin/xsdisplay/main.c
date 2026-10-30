#include <akaos/toolkit.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void usage(void) {
    puts("Usage: xsdisplay [--check|--version]\n"
         "Native X11/Wayland-compatible display protocol bridge\n"
         "  --check    validate the running XSDisplay/Lumen channel\n"
         "  --version  print protocol ABI version");
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--version") == 0 ||
                     strcmp(argv[1], "-V") == 0)) {
        printf("XSDisplay protocol %u (X11/Wayland adapter ABI)\n",
               AKAOS_COMPOSITOR_PROTOCOL_VERSION);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--help") != 0 &&
        strcmp(argv[1], "--check") != 0) {
        usage();
        return 2;
    }
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    }

    struct akaos_display_info info;
    if (akaos_toolkit_display_info(&info) != 0) {
        if (argc > 1 && strcmp(argv[1], "--check") == 0)
            printf("xsdisplay: compositor channel unavailable (errno=%d)\n", errno);
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "--check") == 0) {
        if (info.abi_version == AKAOS_DISPLAY_ABI_VERSION) {
            printf("XSDisplay READY %ux%u ABI=%u protocol=%u\n",
                   info.width, info.height, info.abi_version,
                   AKAOS_COMPOSITOR_PROTOCOL_VERSION);
        } else {
            printf("XSDisplay NOT READY (ABI=%u protocol=%u)\n",
                   info.abi_version, AKAOS_COMPOSITOR_PROTOCOL_VERSION);
        }
    }
    return info.abi_version == AKAOS_DISPLAY_ABI_VERSION ? 0 : 1;
}
