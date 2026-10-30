#include <akaos/fs.h>
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

static void print_error(const char *target, const char *msg) {
    write_str(STDERR_FILENO, "cat: ");
    write_str(STDERR_FILENO, target);
    write_str(STDERR_FILENO, ": ");
    write_str(STDERR_FILENO, msg);
    write_str(STDERR_FILENO, "\n");
}

static int cat_stream(int fd, const char *name) {
    char buffer[128];
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EISDIR) {
                print_error(name, "Is a directory");
            } else {
                print_error(name, "Read error");
            }
            return 1;
        }
        if (count == 0) break;
        size_t written = 0;
        while (written < (size_t)count) {
            ssize_t res = write(STDOUT_FILENO, buffer + written, (size_t)count - written);
            if (res <= 0) return 1;
            written += (size_t)res;
        }
    }
    return 0;
}

static int cat_file(const char *path) {
    struct akaos_stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EACCES) {
            print_error(path, "Permission denied");
        } else {
            print_error(path, "No such file or directory");
        }
        return 1;
    }

    if (st.type == AKAOS_DT_DIRECTORY) {
        print_error(path, "Is a directory");
        return 1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == EISDIR) {
            print_error(path, "Is a directory");
        } else if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EACCES) {
            print_error(path, "Permission denied");
        } else {
            print_error(path, "Error opening file");
        }
        return 1;
    }

    int res = cat_stream(fd, path);
    close(fd);
    return res;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        return cat_stream(STDIN_FILENO, "standard input");
    }

    int had_error = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-") == 0) {
            if (cat_stream(STDIN_FILENO, "standard input") != 0) {
                had_error = 1;
            }
        } else {
            if (cat_file(argv[i]) != 0) {
                had_error = 1;
            }
        }
    }

    return had_error ? 1 : 0;
}
