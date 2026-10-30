#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char pci_buf[4096];

static void print_usage(void) {
    puts("Usage: lspci [options]\n"
         "List all PCI devices in the system.\n\n"
         "Options:\n"
         "  -v, --verbose  be verbose (show BARs and capabilities)\n"
         "  -h, --help     display this help and exit\n"
         "  -V, --version  output version information and exit\n");
}

int main(int argc, char **argv) {
    int verbose = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("lspci (akaOS XSCore)");
            return 0;
        }
    }

    int fd = open("/proc/pci", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "lspci: cannot open /proc/pci\n");
        return 1;
    }
    ssize_t n = read(fd, pci_buf, sizeof(pci_buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "lspci: no PCI devices found\n");
        return 0;
    }
    pci_buf[n] = '\0';

    if (verbose) {
        /* In verbose mode, print full /proc/pci content */
        printf("%s", pci_buf);
        return 0;
    }

    /* In standard mode, parse each device block and print a one-line summary */
    char *line = pci_buf;
    int cur_bus = 0, cur_slot = 0, cur_func = 0;
    char class_desc[64] = "";
    char dev_name[128] = "";
    int have_dev = 0;

    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        if (strncmp(line, "  Bus ", 6) == 0) {
            if (have_dev) {
                printf("%02x:%02x.%x %s: %s\n", cur_bus, cur_slot, cur_func,
                       class_desc[0] ? class_desc : "Device",
                       dev_name[0] ? dev_name : "Unknown Device");
                class_desc[0] = '\0';
                dev_name[0] = '\0';
            }
            have_dev = 1;
            char *p = line + 6;
            cur_bus = atoi(p);
            p = strstr(p, "device ");
            if (p) cur_slot = atoi(p + 7);
            p = strstr(p ? p : line, "function ");
            if (p) cur_func = atoi(p + 9);
        } else if (strncmp(line, "    Class ", 10) == 0) {
            char *colon = strchr(line + 10, ':');
            if (colon) {
                colon++;
                while (*colon == ' ') colon++;
                char *rev = strstr(colon, " (rev");
                size_t clen = rev ? (size_t)(rev - colon) : strlen(colon);
                if (clen >= sizeof(class_desc)) clen = sizeof(class_desc) - 1;
                strncpy(class_desc, colon, clen);
                class_desc[clen] = '\0';
            }
        } else if (strstr(line, "Vendor 0x") && strchr(line, '[')) {
            char *bracket = strchr(line, '[');
            char *end_bracket = strchr(bracket, ']');
            if (bracket && end_bracket && end_bracket > bracket) {
                size_t dlen = (size_t)(end_bracket - (bracket + 1));
                if (dlen >= sizeof(dev_name)) dlen = sizeof(dev_name) - 1;
                strncpy(dev_name, bracket + 1, dlen);
                dev_name[dlen] = '\0';
            }
        }

        if (!eol) break;
        line = eol + 1;
    }

    if (have_dev) {
        printf("%02x:%02x.%x %s: %s\n", cur_bus, cur_slot, cur_func,
               class_desc[0] ? class_desc : "Device",
               dev_name[0] ? dev_name : "Unknown Device");
    }

    return 0;
}
