#include <akaos/architecture.h>
#include <akaos/version.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *SYSNAME  = "XSCore";
static const char *NODENAME = AKAOS_NAME;
static const char *RELEASE  = AKAOS_VERSION;
static const char *VERSION  = "#1 " XSCORE_VERSION;
static const char *OSNAME   = AKAOS_NAME;

int main(int argc, char **argv) {
    int print_s = 0;
    int print_n = 0;
    int print_r = 0;
    int print_v = 0;
    int print_m = 0;
    int print_o = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; argv[i][j]; ++j) {
                switch (argv[i][j]) {
                    case 'a':
                        print_s = print_n = print_r = print_v = print_m = print_o = 1;
                        break;
                    case 's': print_s = 1; break;
                    case 'n': print_n = 1; break;
                    case 'r': print_r = 1; break;
                    case 'v': print_v = 1; break;
                    case 'm': print_m = 1; break;
                    case 'o': print_o = 1; break;
                    default:
                        fprintf(stderr, "uname: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
        } else {
            fprintf(stderr, "uname: extra operand '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!print_s && !print_n && !print_r && !print_v && !print_m && !print_o) {
        print_s = 1;
    }

    int first = 1;
    if (print_s) {
        printf("%s%s", first ? "" : " ", SYSNAME);
        first = 0;
    }
    if (print_n) {
        printf("%s%s", first ? "" : " ", NODENAME);
        first = 0;
    }
    if (print_r) {
        printf("%s%s", first ? "" : " ", RELEASE);
        first = 0;
    }
    if (print_v) {
        printf("%s%s", first ? "" : " ", VERSION);
        first = 0;
    }
    if (print_m) {
        printf("%s%s", first ? "" : " ", AKAOS_ARCH_NAME);
        first = 0;
    }
    if (print_o) {
        printf("%s%s", first ? "" : " ", OSNAME);
        first = 0;
    }
    printf("\n");
    return 0;
}
