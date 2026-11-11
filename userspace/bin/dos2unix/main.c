#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DOS2UNIX_VERSION "7.5.2 (akaOS coreutils)"

typedef enum {
    MODE_ASCII,
    MODE_7BIT,
    MODE_ISO,
    MODE_MAC
} conv_mode_t;

typedef enum {
    EOL_NONE,
    EOL_DOS,
    EOL_UNIX,
    EOL_MAC
} eol_type_t;

static conv_mode_t opt_convmode = MODE_ASCII;
static bool opt_keepdate = false;
static bool opt_force = false;
static bool opt_quiet = false;
static bool opt_verbose = false;
static bool opt_keep_bom = false;
static bool opt_remove_bom = true;
static bool opt_add_bom = false;
static bool opt_add_eol = false;
static bool opt_newline = false;
static bool opt_info = false;
static const char *opt_info_flags = "";
static bool opt_to_stdout = false;
static bool opt_newfile = false;

static void print_version(void) {
    printf("dos2unix %s\n", DOS2UNIX_VERSION);
}

static void print_help(void) {
    printf("Usage: dos2unix [options] [FILE ...] [-n InFILE OutFILE ...]\n"
           "       unix2dos [options] [FILE ...] [-n InFILE OutFILE ...]\n\n"
           "Convert text file line breaks between DOS/Mac and Unix formats.\n\n"
           "Options:\n"
           "  -ascii                default conversion mode\n"
           "  -7                    convert 8 bit characters to 7 bit space\n"
           "  -iso                  convert between DOS and ISO-8859-1\n"
           "  -c, --convmode MODE   conversion mode: ascii, 7bit, iso, mac\n"
           "  -b, --keep-bom        keep Byte Order Mark\n"
           "  -r, --remove-bom      remove Byte Order Mark (default)\n"
           "  -m, --add-bom         add Byte Order Mark\n"
           "  -k, --keepdate        keep output file date stamp same as input\n"
           "  -f, --force           force conversion of binary files\n"
           "  -s, --safe            skip binary files (default)\n"
           "  -q, --quiet           quiet mode, suppress all warnings\n"
           "  -v, --verbose         display verbose messages\n"
           "  -l, --newline         add additional newline\n"
           "  -e, --add-eol         add line break to last line if missing\n"
           "  -o, --oldfile FILE    old file mode (default), overwrite FILE\n"
           "  -n, --newfile IN OUT  new file mode, convert IN and write to OUT\n"
           "  -O, --to-stdout       write to standard output\n"
           "  -i, --info[=FLAGS]    display file information\n"
           "  -h, --help            display this help and exit\n"
           "  -V, --version         display version information and exit\n");
}

static int is_binary_buffer(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == 0) return 1;
    }
    return 0;
}

static int print_file_info(const char *path, const char *flags) {
    int fd = STDIN_FILENO;
    if (path && strcmp(path, "-") != 0) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            if (!opt_quiet) perror(path);
            return 1;
        }
    }

    uint64_t dos_count = 0;
    uint64_t unix_count = 0;
    uint64_t mac_count = 0;
    const char *bom = "no_bom";
    int is_binary = 0;
    eol_type_t last_eol = EOL_NONE;

    unsigned char buf[4096];
    ssize_t n;
    int first_chunk = 1;
    unsigned char prev = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (first_chunk) {
            first_chunk = 0;
            if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
                bom = "UTF-8";
            } else if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
                bom = "UTF-16LE";
            } else if (n >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
                bom = "UTF-16BE";
            }
        }
        if (!is_binary && is_binary_buffer(buf, (size_t)n)) {
            is_binary = 1;
        }

        for (ssize_t i = 0; i < n; ++i) {
            unsigned char c = buf[i];
            if (prev == '\r') {
                if (c == '\n') {
                    dos_count++;
                    last_eol = EOL_DOS;
                    prev = 0;
                    continue;
                } else {
                    mac_count++;
                    last_eol = EOL_MAC;
                }
            }
            if (c == '\n') {
                unix_count++;
                last_eol = EOL_UNIX;
            }
            prev = c;
        }
    }

    if (prev == '\r') {
        mac_count++;
        last_eol = EOL_MAC;
    }

    if (fd != STDIN_FILENO) close(fd);

    const char *display_path = path ? path : "stdin";
    if (strchr(flags, 'p')) {
        const char *slash = strrchr(display_path, '/');
        if (slash) display_path = slash + 1;
    }

    const char *last_eol_str = "noeol";
    if (last_eol == EOL_DOS) last_eol_str = "dos";
    else if (last_eol == EOL_UNIX) last_eol_str = "unix";
    else if (last_eol == EOL_MAC) last_eol_str = "mac";

    if (strchr(flags, 'c')) {
        if (dos_count > 0 || (opt_add_eol && last_eol == EOL_NONE)) {
            printf("%s%c", display_path, strchr(flags, '0') ? '\0' : '\n');
        }
        return 0;
    }

    char term = strchr(flags, '0') ? '\0' : '\n';
    if (opt_add_eol || strchr(flags, 'e')) {
        printf("%6lu  %6lu  %6lu  %-8s  %-6s  %-6s  %s%c",
               (unsigned long)dos_count, (unsigned long)unix_count, (unsigned long)mac_count,
               bom, is_binary ? "binary" : "text", last_eol_str, display_path, term);
    } else {
        printf("%6lu  %6lu  %6lu  %-8s  %-6s  %s%c",
               (unsigned long)dos_count, (unsigned long)unix_count, (unsigned long)mac_count,
               bom, is_binary ? "binary" : "text", display_path, term);
    }
    return 0;
}

static int convert_stream(int in_fd, int out_fd, int is_unix2dos) {
    unsigned char in_buf[4096];
    unsigned char out_buf[8192];
    size_t out_len = 0;
    ssize_t n;
    int first_chunk = 1;
    int has_bom = 0;
    unsigned char prev = 0;
    int binary_detected = 0;

    while ((n = read(in_fd, in_buf, sizeof(in_buf))) > 0) {
        size_t offset = 0;
        if (first_chunk) {
            first_chunk = 0;
            if (n >= 3 && in_buf[0] == 0xEF && in_buf[1] == 0xBB && in_buf[2] == 0xBF) {
                has_bom = 1;
                if (opt_remove_bom && !opt_keep_bom && !opt_add_bom) {
                    offset = 3;
                }
            }
            if (opt_add_bom || (opt_keep_bom && has_bom)) {
                out_buf[out_len++] = 0xEF;
                out_buf[out_len++] = 0xBB;
                out_buf[out_len++] = 0xBF;
            }
        }

        if (!opt_force && !binary_detected && is_binary_buffer(in_buf + offset, (size_t)n - offset)) {
            binary_detected = 1;
            if (!opt_quiet) fprintf(stderr, "dos2unix: skipping binary file\n");
            return 1;
        }

        for (size_t i = offset; i < (size_t)n; ++i) {
            unsigned char c = in_buf[i];

            if (opt_convmode == MODE_7BIT && (c & 0x80)) {
                c = ' ';
            }

            if (!is_unix2dos) {
                /* dos2unix logic: convert \r\n to \n, or \r to \n in MAC mode */
                if (prev == '\r') {
                    if (c == '\n') {
                        out_buf[out_len++] = '\n';
                        if (opt_newline) out_buf[out_len++] = '\n';
                        prev = 0;
                        continue;
                    } else if (opt_convmode == MODE_MAC) {
                        out_buf[out_len++] = '\n';
                        if (opt_newline) out_buf[out_len++] = '\n';
                        prev = 0;
                    } else {
                        out_buf[out_len++] = '\r';
                        prev = 0;
                    }
                }
                if (c == '\r') {
                    prev = '\r';
                    continue;
                }
                out_buf[out_len++] = c;
            } else {
                /* unix2dos logic: convert \n to \r\n */
                if (c == '\n' && prev != '\r') {
                    if (opt_convmode == MODE_MAC) {
                        out_buf[out_len++] = '\r';
                        if (opt_newline) out_buf[out_len++] = '\r';
                    } else {
                        out_buf[out_len++] = '\r';
                        out_buf[out_len++] = '\n';
                        if (opt_newline) {
                            out_buf[out_len++] = '\r';
                            out_buf[out_len++] = '\n';
                        }
                    }
                } else {
                    out_buf[out_len++] = c;
                }
                prev = c;
            }

            if (out_len >= sizeof(out_buf) - 16) {
                if (write(out_fd, out_buf, out_len) != (ssize_t)out_len) return -1;
                out_len = 0;
            }
        }
    }

    if (prev == '\r') {
        if (!is_unix2dos && opt_convmode == MODE_MAC) {
            out_buf[out_len++] = '\n';
            if (opt_newline) out_buf[out_len++] = '\n';
        } else {
            out_buf[out_len++] = '\r';
        }
    }

    if (opt_add_eol && prev != '\n') {
        if (is_unix2dos && opt_convmode != MODE_MAC) {
            out_buf[out_len++] = '\r';
            out_buf[out_len++] = '\n';
        } else {
            out_buf[out_len++] = '\n';
        }
    }

    if (out_len > 0) {
        if (write(out_fd, out_buf, out_len) != (ssize_t)out_len) return -1;
    }
    return 0;
}

static int convert_file(const char *in_path, const char *out_path, int is_unix2dos) {
    int in_fd = STDIN_FILENO;
    struct stat st;
    bool is_stdin = (!in_path || strcmp(in_path, "-") == 0);

    if (!is_stdin) {
        if (stat(in_path, &st) != 0) {
            if (!opt_quiet) perror(in_path);
            return 1;
        }
        in_fd = open(in_path, O_RDONLY);
        if (in_fd < 0) {
            if (!opt_quiet) perror(in_path);
            return 1;
        }
    }

    char tmp_path[512];
    int out_fd = STDOUT_FILENO;
    bool is_stdout = (opt_to_stdout || (!out_path && is_stdin) || (out_path && strcmp(out_path, "-") == 0));

    if (!is_stdout) {
        if (out_path) {
            strncpy(tmp_path, out_path, sizeof(tmp_path) - 1);
            tmp_path[sizeof(tmp_path) - 1] = '\0';
            out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, is_stdin ? 0644 : (st.st_mode & 0777));
        } else {
            snprintf(tmp_path, sizeof(tmp_path), "%s.d2u_tmp_XXXXXX", in_path);
            int mfd = mkstemp(tmp_path);
            if (mfd < 0) {
                /* Fallback to simple temporary path */
                snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", in_path);
                out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
            } else {
                out_fd = mfd;
                fchmod(out_fd, st.st_mode & 0777);
            }
        }
        if (out_fd < 0) {
            if (!opt_quiet) perror(tmp_path);
            if (!is_stdin) close(in_fd);
            return 1;
        }
    }

    int res = convert_stream(in_fd, out_fd, is_unix2dos);

    if (!is_stdin) close(in_fd);
    if (!is_stdout) close(out_fd);

    if (res != 0) {
        if (!is_stdout && !out_path) unlink(tmp_path);
        return (res > 0) ? 0 : 1; /* Binary file skipped is not an error */
    }

    if (!is_stdout && !out_path) {
        if (rename(tmp_path, in_path) != 0) {
            if (!opt_quiet) perror("rename");
            unlink(tmp_path);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int is_unix2dos = 0;
    if (strstr(argv[0], "unix2dos")) {
        is_unix2dos = 1;
    }

    int arg_idx = 1;
    bool end_of_opts = false;
    char *in_files[256];
    char *out_files[256];
    int pair_count = 0;
    int single_count = 0;

    while (arg_idx < argc) {
        char *arg = argv[arg_idx];
        if (!end_of_opts && strcmp(arg, "--") == 0) {
            end_of_opts = true;
            arg_idx++;
            continue;
        }
        if (!end_of_opts && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                print_help();
                return 0;
            }
            if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
                print_version();
                return 0;
            }
            if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
                opt_quiet = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
                opt_verbose = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-k") == 0 || strcmp(arg, "--keepdate") == 0) {
                opt_keepdate = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
                opt_force = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-s") == 0 || strcmp(arg, "--safe") == 0) {
                opt_force = false;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-b") == 0 || strcmp(arg, "--keep-bom") == 0) {
                opt_keep_bom = true;
                opt_remove_bom = false;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-r") == 0 || strcmp(arg, "--remove-bom") == 0) {
                opt_remove_bom = true;
                opt_keep_bom = false;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-m") == 0 || strcmp(arg, "--add-bom") == 0) {
                opt_add_bom = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-l") == 0 || strcmp(arg, "--newline") == 0) {
                opt_newline = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-e") == 0 || strcmp(arg, "--add-eol") == 0) {
                opt_add_eol = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-O") == 0 || strcmp(arg, "--to-stdout") == 0) {
                opt_to_stdout = true;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-o") == 0 || strcmp(arg, "--oldfile") == 0) {
                opt_newfile = false;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-ascii") == 0) {
                opt_convmode = MODE_ASCII;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-7") == 0) {
                opt_convmode = MODE_7BIT;
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-iso") == 0 || strcmp(arg, "-1252") == 0 || strcmp(arg, "-437") == 0 ||
                strcmp(arg, "-850") == 0 || strcmp(arg, "-860") == 0 || strcmp(arg, "-863") == 0 ||
                strcmp(arg, "-865") == 0) {
                opt_convmode = MODE_ISO;
                arg_idx++;
                continue;
            }
            if (strncmp(arg, "-c", 2) == 0 || strcmp(arg, "--convmode") == 0) {
                const char *mode = NULL;
                if (strcmp(arg, "-c") == 0 || strcmp(arg, "--convmode") == 0) {
                    if (arg_idx + 1 < argc) mode = argv[++arg_idx];
                } else {
                    mode = arg + 2;
                }
                if (mode) {
                    if (strcmp(mode, "mac") == 0) opt_convmode = MODE_MAC;
                    else if (strcmp(mode, "7bit") == 0) opt_convmode = MODE_7BIT;
                    else if (strcmp(mode, "iso") == 0) opt_convmode = MODE_ISO;
                    else opt_convmode = MODE_ASCII;
                }
                arg_idx++;
                continue;
            }
            if (strncmp(arg, "-i", 2) == 0 || strncmp(arg, "--info", 6) == 0) {
                opt_info = true;
                if (strncmp(arg, "-i", 2) == 0 && arg[2] != '\0') {
                    opt_info_flags = arg + 2;
                } else if (strncmp(arg, "--info=", 7) == 0) {
                    opt_info_flags = arg + 7;
                } else {
                    opt_info_flags = "";
                }
                arg_idx++;
                continue;
            }
            if (strcmp(arg, "-n") == 0 || strcmp(arg, "--newfile") == 0) {
                opt_newfile = true;
                arg_idx++;
                continue;
            }
            /* Ignore Windows-specific / no-op options safely */
            if (strcmp(arg, "--allow-chown") == 0 || strcmp(arg, "--no-allow-chown") == 0 ||
                strcmp(arg, "--no-add-eol") == 0 || strcmp(arg, "--error-binary") == 0 ||
                strcmp(arg, "--no-error-binary") == 0 || strcmp(arg, "-u") == 0 ||
                strcmp(arg, "-ul") == 0 || strcmp(arg, "-ub") == 0 || strcmp(arg, "-gb") == 0 ||
                strcmp(arg, "-F") == 0 || strcmp(arg, "-R") == 0 || strcmp(arg, "-S") == 0) {
                arg_idx++;
                continue;
            }
            fprintf(stderr, "dos2unix: invalid option '%s'\n", arg);
            return 1;
        }

        if (opt_newfile) {
            if (arg_idx + 1 < argc) {
                in_files[pair_count] = argv[arg_idx++];
                out_files[pair_count] = argv[arg_idx++];
                pair_count++;
            } else {
                fprintf(stderr, "dos2unix: option -n requires pair of arguments\n");
                return 1;
            }
        } else {
            in_files[single_count++] = argv[arg_idx++];
        }
    }

    if (opt_info) {
        if (single_count == 0 && pair_count == 0) {
            return print_file_info("-", opt_info_flags);
        }
        int status = 0;
        if (strchr(opt_info_flags, 'h')) {
            printf("  DOS    UNIX     MAC  BOM       Format  Filename\n");
        }
        for (int i = 0; i < single_count; ++i) {
            if (print_file_info(in_files[i], opt_info_flags) != 0) status = 1;
        }
        return status;
    }

    if (pair_count == 0 && single_count == 0) {
        return convert_file("-", "-", is_unix2dos);
    }

    int overall_status = 0;
    for (int i = 0; i < pair_count; ++i) {
        if (convert_file(in_files[i], out_files[i], is_unix2dos) != 0) {
            overall_status = 1;
        }
    }
    for (int i = 0; i < single_count; ++i) {
        if (convert_file(in_files[i], NULL, is_unix2dos) != 0) {
            overall_status = 1;
        }
    }

    return overall_status;
}
