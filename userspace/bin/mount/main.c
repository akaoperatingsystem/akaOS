#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>

static void print_usage(void) {
    printf("Usage:\n");
    printf("  mount                      List mounted filesystems\n");
    printf("  mount -a                   Mount all filesystems in /etc/fstab\n");
    printf("  mount [-t type] [-o opt] <dev> <dir>\n");
}

static int parse_mount_line(char *line, char *src, size_t src_sz,
                            char *tgt, size_t tgt_sz,
                            char *fst, size_t fst_sz,
                            char *opt, size_t opt_sz) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#') return 0;

    /* Token 1: src */
    char *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t len = (size_t)(p - start);
    if (len >= src_sz) len = src_sz - 1;
    memcpy(src, start, len);
    src[len] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 1;

    /* Token 2: tgt */
    start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    len = (size_t)(p - start);
    if (len >= tgt_sz) len = tgt_sz - 1;
    memcpy(tgt, start, len);
    tgt[len] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 2;

    /* Token 3: fst */
    start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    len = (size_t)(p - start);
    if (len >= fst_sz) len = fst_sz - 1;
    memcpy(fst, start, len);
    fst[len] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 3;

    /* Token 4: opt */
    start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    len = (size_t)(p - start);
    if (len >= opt_sz) len = opt_sz - 1;
    memcpy(opt, start, len);
    opt[len] = '\0';

    return 4;
}

static int print_mounts(void) {
    int fd = open("/proc/mounts", O_RDONLY);
    if (fd < 0) {
        perror("mount: cannot open /proc/mounts");
        return 1;
    }
    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        /* Format nicely: src target fstype opt -> src on target type fstype (opt) */
        char *line = buf;
        while (*line) {
            char *eol = strchr(line, '\n');
            if (eol) *eol = '\0';
            if (*line) {
                char src[64] = {0}, tgt[128] = {0}, fst[32] = {0}, opt[32] = {0};
                if (parse_mount_line(line, src, sizeof(src), tgt, sizeof(tgt), fst, sizeof(fst), opt, sizeof(opt)) >= 3) {
                    printf("%s on %s type %s (%s)\n", src, tgt, fst, opt[0] ? opt : "rw");
                } else {
                    printf("%s\n", line);
                }
            }
            if (!eol) break;
            line = eol + 1;
        }
    }
    close(fd);
    return 0;
}

static int mount_entry(const char *src, const char *tgt, const char *fst, unsigned long flags) {
    if (mount(src, tgt, fst ? fst : "akafs", flags, NULL) != 0) {
        fprintf(stderr, "mount: mounting %s on %s failed: %s\n", src, tgt, strerror(errno));
        return 1;
    }
    return 0;
}

static int mount_all_fstab(void) {
    int fd = open("/etc/fstab", O_RDONLY);
    if (fd < 0) {
        perror("mount: cannot open /etc/fstab");
        return 1;
    }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char *line = buf;
    int errs = 0;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '#') {
            char src[64] = {0}, tgt[128] = {0}, fst[32] = {0}, opt[32] = {0};
            if (parse_mount_line(line, src, sizeof(src), tgt, sizeof(tgt), fst, sizeof(fst), opt, sizeof(opt)) >= 3) {
                unsigned long flags = 0;
                if (strstr(opt, "ro")) flags |= MS_RDONLY;
                if (strstr(opt, "remount")) flags |= MS_REMOUNT;
                /* If root or already active, continue */
                if (strcmp(tgt, "/") != 0 && strcmp(tgt, "/dev") != 0 && strcmp(tgt, "/proc") != 0) {
                    errs += mount_entry(src, tgt, fst, flags);
                }
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    return errs > 0 ? 1 : 0;
}


int main(int argc, char **argv) {
    if (argc == 1) {
        return print_mounts();
    }

    const char *fstype = "akafs";
    const char *source = NULL;
    const char *target = NULL;
    unsigned long flags = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-a") == 0) {
            return mount_all_fstab();
        }
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            fstype = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            const char *opt = argv[++i];
            if (strstr(opt, "ro")) flags |= MS_RDONLY;
            if (strstr(opt, "remount")) flags |= MS_REMOUNT;
        } else if (!source) {
            source = argv[i];
        } else if (!target) {
            target = argv[i];
        }
    }

    if (!source || !target) {
        print_usage();
        return 1;
    }

    return mount_entry(source, target, fstype, flags);
}
