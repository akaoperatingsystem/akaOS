#include <akaos/fs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_str(int fd, const char *str) {
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

static void print_usage(void) {
    write_str(STDOUT_FILENO, "Usage: rm [OPTION]... [FILE]...\n");
    write_str(STDOUT_FILENO, "Remove (unlink) the FILE(s).\n\n");
    write_str(STDOUT_FILENO, "  -f, --force           ignore nonexistent files and arguments, never prompt\n");
    write_str(STDOUT_FILENO, "  -r, -R, --recursive   remove directories and their contents recursively\n");
    write_str(STDOUT_FILENO, "  -d, --dir             remove empty directories\n");
    write_str(STDOUT_FILENO, "  -v, --verbose         explain what is being done\n");
    write_str(STDOUT_FILENO, "  -h, --help            display this help and exit\n\n");
    write_str(STDOUT_FILENO, "By default, rm does not remove directories.  Use the --recursive (-r or -R)\n");
    write_str(STDOUT_FILENO, "option to remove each listed directory, too, along with all of its contents.\n");
}

static void print_error(const char *path, const char *msg) {
    write_str(STDERR_FILENO, "rm: cannot remove '");
    write_str(STDERR_FILENO, path);
    write_str(STDERR_FILENO, "': ");
    write_str(STDERR_FILENO, msg);
    write_str(STDERR_FILENO, "\n");
}

static int remove_entry(const char *path, int opt_force, int opt_recursive,
                        int opt_dir, int opt_verbose) {
    struct akaos_stat st;
    if (stat(path, &st) != 0) {
        if (opt_force) return 0;
        if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EPERM) {
            print_error(path, "Operation not permitted");
        } else {
            print_error(path, "Error accessing path");
        }
        return 1;
    }

    if (st.type != AKAOS_DT_DIRECTORY) {
        if (unlink(path) != 0) {
            if (opt_force) return 0;
            if (errno == ENOENT) {
                print_error(path, "No such file or directory");
            } else if (errno == EPERM) {
                print_error(path, "Operation not permitted");
            } else {
                print_error(path, "Error removing file");
            }
            return 1;
        }
        if (opt_verbose) {
            write_str(STDOUT_FILENO, "removed '");
            write_str(STDOUT_FILENO, path);
            write_str(STDOUT_FILENO, "'\n");
        }
        return 0;
    }

    /* Target is a directory */
    if (!opt_recursive && !opt_dir) {
        if (opt_force) return 0;
        print_error(path, "Is a directory");
        return 1;
    }

    if (opt_dir && !opt_recursive) {
        if (unlink(path) != 0) {
            if (opt_force) return 0;
            if (errno == ENOTEMPTY) {
                print_error(path, "Directory not empty");
            } else if (errno == EPERM) {
                print_error(path, "Operation not permitted");
            } else if (errno == ENOENT) {
                print_error(path, "No such file or directory");
            } else {
                print_error(path, "Cannot remove directory");
            }
            return 1;
        }
        if (opt_verbose) {
            write_str(STDOUT_FILENO, "removed directory '");
            write_str(STDOUT_FILENO, path);
            write_str(STDOUT_FILENO, "'\n");
        }
        return 0;
    }

    /* Recursive directory removal */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (opt_force) return 0;
        print_error(path, "Cannot open directory");
        return 1;
    }

    struct akaos_dirent entries[4];
    ssize_t count;
    int had_child_error = 0;

    while ((count = getdents(fd, entries, 4)) > 0) {
        for (ssize_t i = 0; i < count; ++i) {
            const char *name = entries[i].name;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

            char sub_path[128];
            size_t plen = strlen(path);
            size_t nlen = strlen(name);
            while (plen > 1 && path[plen - 1] == '/') --plen;

            if (plen + 1 + nlen >= sizeof(sub_path)) {
                had_child_error = 1;
                continue;
            }

            memcpy(sub_path, path, plen);
            sub_path[plen] = '/';
            memcpy(sub_path + plen + 1, name, nlen);
            sub_path[plen + 1 + nlen] = '\0';

            if (remove_entry(sub_path, opt_force, opt_recursive, opt_dir, opt_verbose) != 0) {
                had_child_error = 1;
            }
        }
    }

    close(fd);

    if (had_child_error && !opt_force) return 1;

    if (unlink(path) != 0) {
        if (opt_force) return 0;
        if (errno == ENOTEMPTY) {
            print_error(path, "Directory not empty");
        } else if (errno == EPERM) {
            print_error(path, "Operation not permitted");
        } else {
            print_error(path, "Cannot remove directory");
        }
        return 1;
    }

    if (opt_verbose) {
        write_str(STDOUT_FILENO, "removed directory '");
        write_str(STDOUT_FILENO, path);
        write_str(STDOUT_FILENO, "'\n");
    }

    return 0;
}

int main(int argc, char **argv) {
    int opt_force = 0;
    int opt_recursive = 0;
    int opt_dir = 0;
    int opt_verbose = 0;

    const char *operands[64];
    int operand_count = 0;
    int parse_options = 1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_options && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--") == 0) {
                parse_options = 0;
                continue;
            }
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
                print_usage();
                return 0;
            }
            if (strcmp(arg, "--force") == 0) {
                opt_force = 1;
                continue;
            }
            if (strcmp(arg, "--recursive") == 0) {
                opt_recursive = 1;
                continue;
            }
            if (strcmp(arg, "--dir") == 0) {
                opt_dir = 1;
                continue;
            }
            if (strcmp(arg, "--verbose") == 0) {
                opt_verbose = 1;
                continue;
            }

            if (arg[1] == '-') {
                write_str(STDERR_FILENO, "rm: unrecognized option '");
                write_str(STDERR_FILENO, arg);
                write_str(STDERR_FILENO, "'\nTry 'rm --help' for more information.\n");
                return 1;
            }

            /* Single-character options (possibly combined, e.g. -rf) */
            int valid = 1;
            for (size_t j = 1; arg[j] != '\0'; ++j) {
                char c = arg[j];
                if (c == 'f') {
                    opt_force = 1;
                } else if (c == 'r' || c == 'R') {
                    opt_recursive = 1;
                } else if (c == 'd') {
                    opt_dir = 1;
                } else if (c == 'v') {
                    opt_verbose = 1;
                } else if (c == 'h') {
                    print_usage();
                    return 0;
                } else {
                    write_str(STDERR_FILENO, "rm: invalid option -- '");
                    char err_char[2] = { c, '\0' };
                    write_str(STDERR_FILENO, err_char);
                    write_str(STDERR_FILENO, "'\nTry 'rm --help' for more information.\n");
                    valid = 0;
                    break;
                }
            }
            if (!valid) return 1;
        } else {
            if (operand_count < 64) {
                operands[operand_count++] = arg;
            }
        }
    }

    if (operand_count == 0) {
        if (opt_force) return 0;
        write_str(STDERR_FILENO, "rm: missing operand\nTry 'rm --help' for more information.\n");
        return 1;
    }

    int had_error = 0;
    for (int i = 0; i < operand_count; ++i) {
        if (remove_entry(operands[i], opt_force, opt_recursive, opt_dir, opt_verbose) != 0) {
            had_error = 1;
        }
    }

    return had_error ? 1 : 0;
}
