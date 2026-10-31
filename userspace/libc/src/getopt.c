/*
 * getopt / getopt_long implementation for akaOS libc.
 */
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

static int nextchar_idx = 0;

static void print_error(const char *argv0, const char *msg, char opt) {
    if (opterr) {
        if (opt) {
            fprintf(stderr, "%s: %s -- '%c'\n", argv0, msg, opt);
        } else {
            fprintf(stderr, "%s: %s\n", argv0, msg);
        }
    }
}

int getopt(int argc, char * const argv[], const char *optstring) {
    optarg = NULL;
    if (optind >= argc || !argv[optind]) {
        return -1;
    }

    char *arg = argv[optind];
    if (arg[0] != '-' || arg[1] == '\0') {
        return -1;
    }

    if (strcmp(arg, "--") == 0) {
        optind++;
        return -1;
    }

    if (nextchar_idx == 0) {
        nextchar_idx = 1;
    }

    char c = arg[nextchar_idx++];
    const char *spec = strchr(optstring, c);
    if (!spec || c == ':') {
        optopt = c;
        print_error(argv[0], "invalid option", c);
        if (arg[nextchar_idx] == '\0') {
            optind++;
            nextchar_idx = 0;
        }
        return '?';
    }

    if (spec[1] == ':') {
        if (arg[nextchar_idx] != '\0') {
            optarg = &arg[nextchar_idx];
            optind++;
            nextchar_idx = 0;
        } else if (spec[2] == ':') {
            /* Optional argument */
            optarg = NULL;
            optind++;
            nextchar_idx = 0;
        } else {
            /* Required argument */
            optind++;
            nextchar_idx = 0;
            if (optind >= argc) {
                optopt = c;
                if (optstring[0] == ':') {
                    return ':';
                }
                print_error(argv[0], "option requires an argument", c);
                return '?';
            }
            optarg = argv[optind++];
        }
    } else {
        if (arg[nextchar_idx] == '\0') {
            optind++;
            nextchar_idx = 0;
        }
    }

    return (int)(unsigned char)c;
}

int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    optarg = NULL;
    if (optind >= argc || !argv[optind]) {
        return -1;
    }

    char *arg = argv[optind];
    if (arg[0] != '-' || arg[1] == '\0') {
        return -1;
    }

    if (strcmp(arg, "--") == 0) {
        optind++;
        return -1;
    }

    /* Long option check: starts with -- */
    if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
        const char *name = arg + 2;
        const char *eq = strchr(name, '=');
        size_t name_len = eq ? (size_t)(eq - name) : strlen(name);

        int match = -1;
        if (longopts) {
            for (int i = 0; longopts[i].name != NULL; ++i) {
                if (strncmp(longopts[i].name, name, name_len) == 0 &&
                    strlen(longopts[i].name) == name_len) {
                    match = i;
                    break;
                }
            }
        }

        if (match >= 0) {
            if (longindex) *longindex = match;
            optind++;
            if (longopts[match].has_arg == required_argument) {
                if (eq) {
                    optarg = (char *)(eq + 1);
                } else if (optind < argc) {
                    optarg = argv[optind++];
                } else {
                    if (opterr) {
                        fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], longopts[match].name);
                    }
                    optopt = longopts[match].val;
                    return (optstring && optstring[0] == ':') ? ':' : '?';
                }
            } else if (longopts[match].has_arg == optional_argument) {
                if (eq) {
                    optarg = (char *)(eq + 1);
                } else {
                    optarg = NULL;
                }
            } else {
                if (eq && opterr) {
                    fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0], longopts[match].name);
                }
                optarg = NULL;
            }

            if (longopts[match].flag) {
                *longopts[match].flag = longopts[match].val;
                return 0;
            }
            return longopts[match].val;
        }

        if (opterr) {
            fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], name);
        }
        optind++;
        return '?';
    }

    return getopt(argc, argv, optstring);
}

int getopt_long_only(int argc, char * const argv[], const char *optstring,
                     const struct option *longopts, int *longindex) {
    return getopt_long(argc, argv, optstring, longopts, longindex);
}
