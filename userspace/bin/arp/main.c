#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char file_buf[2048];

static void print_usage(void) {
    puts("Usage: arp [-vn] [-H type] [-i if] -a [hostname]\n"
         "       arp [-v] [-i if] -d hostname\n"
         "       arp [-v] [-H type] [-i if] -s hostname hw_addr\n\n"
         "Options:\n"
         "  -a             display (all) hosts in alternative (BSD) style\n"
         "  -n, --numeric  don't resolve names\n"
         "  -d             delete a specified entry\n"
         "  -s             set a new ARP entry\n"
         "  -h, --help     display this help and exit\n"
         "  -v, --version  display version information and exit");
}

static char *next_token(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '\0') return NULL;
    char *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    if (**p) {
        **p = '\0';
        (*p)++;
    }
    return start;
}

static int show_arp(int numeric) {
    (void)numeric;
    int fd = open("/proc/net/arp", O_RDONLY);
    if (fd < 0) {
        puts("arp: cannot open /proc/net/arp");
        return 1;
    }
    ssize_t n = read(fd, file_buf, sizeof(file_buf) - 1);
    close(fd);
    if (n <= 0) {
        puts("arp: empty /proc/net/arp");
        return 1;
    }
    file_buf[n] = '\0';

    puts("Address                  HWtype  HWaddress           Flags Mask            Iface");

    char *line = file_buf;
    char *nl = strchr(line, '\n');
    if (nl) line = nl + 1;

    while (*line) {
        char *next_nl = strchr(line, '\n');
        if (next_nl) *next_nl = '\0';

        char *p = line;
        char *ip = next_token(&p);
        char *hw_type = next_token(&p);
        char *flags = next_token(&p);
        char *mac = next_token(&p);
        char *mask = next_token(&p);
        char *iface = next_token(&p);

        (void)hw_type;
        if (ip && flags && mac && mask && iface) {
            const char *type_name = "ether";
            const char *flag_str = "C";
            if (strcmp(flags, "0x4") == 0 || strcmp(flags, "0x04") == 0) flag_str = "CM";
            else if (strcmp(flags, "0x0") == 0) flag_str = "I";

            printf("%-24s %-7s %-19s %-10s %-15s %s\n",
                   ip, type_name, mac, flag_str, mask, iface);
        }

        if (!next_nl) break;
        line = next_nl + 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int numeric = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            puts("arp (akaOS Network Utilities) 1.0");
            return 0;
        }
        if (strcmp(arg, "-n") == 0 || strcmp(arg, "--numeric") == 0) {
            numeric = 1;
        } else if (strcmp(arg, "-d") == 0) {
            return 0;
        } else if (strcmp(arg, "-s") == 0) {
            return 0;
        }
    }

    return show_arp(numeric);
}
