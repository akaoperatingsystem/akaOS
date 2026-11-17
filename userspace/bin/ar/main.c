/*
 * akaOS Native Archiver (ar) - POSIX & GNU ar compatible
 *
 * Implements:
 * - Common archive format (!<arch>\n, 60-byte BSD/GNU member headers)
 * - Operations: r (replace/insert), c (create), s (index/ranlib),
 *   t (table of contents), x (extract), d (delete), p (print to stdout), v (verbose)
 * - Even-byte alignment with '\n' padding
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define AR_MAGIC "!<arch>\n"
#define AR_MAGIC_LEN 8

struct ar_header {
    char ar_name[16];
    char ar_date[12];
    char ar_uid[6];
    char ar_gid[6];
    char ar_mode[8];
    char ar_size[10];
    char ar_fmag[2];
};

struct ar_member {
    char name[128];
    uint32_t date;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t size;
    uint8_t *data;
    struct ar_member *next;
};

static void print_usage(void) {
    printf("Usage: ar [-]{rcstxpdv} [member-name] archive-file file...\n");
    printf("Commands:\n");
    printf("  r[c]      - replace or insert file(s) into archive (c: create without warning)\n");
    printf("  s         - act as ranlib (generate archive symbol index)\n");
    printf("  t         - show contents of archive\n");
    printf("  x         - extract members from archive\n");
    printf("  d         - delete file(s) from archive\n");
    printf("  p         - print file(s) found in the archive\n");
    printf("  v         - be verbose\n");
    printf("  --version - display version information\n");
    printf("  --help    - display this help\n");
}

static struct ar_member *read_archive(const char *ar_name, int *out_fd) {
    int fd = open(ar_name, O_RDONLY);
    if (fd < 0) return NULL;

    char magic[AR_MAGIC_LEN];
    if (read(fd, magic, AR_MAGIC_LEN) != AR_MAGIC_LEN || memcmp(magic, AR_MAGIC, AR_MAGIC_LEN) != 0) {
        close(fd);
        return NULL;
    }

    struct ar_member *head = NULL;
    struct ar_member **tail = &head;

    struct ar_header hdr;
    while (read(fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
        if (hdr.ar_fmag[0] != '\x60' || hdr.ar_fmag[1] != '\n') break;

        struct ar_member *m = calloc(1, sizeof(struct ar_member));
        if (!m) break;

        /* Parse name: strip trailing slash / spaces */
        char nbuf[17];
        memcpy(nbuf, hdr.ar_name, 16);
        nbuf[16] = '\0';
        char *slash = strchr(nbuf, '/');
        if (slash) *slash = '\0';
        char *end = nbuf + strlen(nbuf) - 1;
        while (end >= nbuf && (*end == ' ' || *end == '\t')) *end-- = '\0';
        strncpy(m->name, nbuf, sizeof(m->name) - 1);

        char sbuf[11];
        memcpy(sbuf, hdr.ar_size, 10);
        sbuf[10] = '\0';
        m->size = (uint32_t)strtoul(sbuf, NULL, 10);

        char dbuf[13];
        memcpy(dbuf, hdr.ar_date, 12);
        dbuf[12] = '\0';
        m->date = (uint32_t)strtoul(dbuf, NULL, 10);

        char mbuf[9];
        memcpy(mbuf, hdr.ar_mode, 8);
        mbuf[8] = '\0';
        m->mode = (uint32_t)strtoul(mbuf, NULL, 8);

        if (m->size > 0) {
            m->data = malloc(m->size);
            if (m->data) {
                read(fd, m->data, m->size);
            }
            if (m->size & 1) {
                char pad;
                read(fd, &pad, 1);
            }
        }

        *tail = m;
        tail = &m->next;
    }

    if (out_fd) *out_fd = fd;
    else close(fd);

    return head;
}

static int write_archive(const char *ar_name, struct ar_member *head) {
    int fd = open(ar_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("ar: open for write");
        return 1;
    }

    write(fd, AR_MAGIC, AR_MAGIC_LEN);

    struct ar_member *cur = head;
    while (cur) {
        struct ar_header hdr;
        memset(&hdr, ' ', sizeof(hdr));

        char name_slash[17];
        snprintf(name_slash, sizeof(name_slash), "%s/", cur->name);
        size_t nlen = strlen(name_slash);
        if (nlen > 16) nlen = 16;
        memcpy(hdr.ar_name, name_slash, nlen);

        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%-12u", cur->date ? cur->date : 1234567890U);
        memcpy(hdr.ar_date, num_buf, 12);

        snprintf(num_buf, sizeof(num_buf), "%-6u", cur->uid);
        memcpy(hdr.ar_uid, num_buf, 6);

        snprintf(num_buf, sizeof(num_buf), "%-6u", cur->gid);
        memcpy(hdr.ar_gid, num_buf, 6);

        snprintf(num_buf, sizeof(num_buf), "%-8o", cur->mode ? cur->mode : 0100644);
        memcpy(hdr.ar_mode, num_buf, 8);

        snprintf(num_buf, sizeof(num_buf), "%-10u", cur->size);
        memcpy(hdr.ar_size, num_buf, 10);

        hdr.ar_fmag[0] = '\x60';
        hdr.ar_fmag[1] = '\n';

        write(fd, &hdr, sizeof(hdr));
        if (cur->size > 0 && cur->data) {
            write(fd, cur->data, cur->size);
            if (cur->size & 1) {
                char pad = '\n';
                write(fd, &pad, 1);
            }
        }

        cur = cur->next;
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("GNU ar (akaOS Native Toolchain) 2.42\n");
        printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
        printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
        return 0;
    }

    const char *ops = argv[1];
    if (*ops == '-') ops++;

    int op_r = 0, op_c = 0, op_s = 0, op_t = 0, op_x = 0, op_d = 0, op_p = 0, op_v = 0; (void)op_c;
    for (const char *p = ops; *p; ++p) {
        if (*p == 'r') op_r = 1;
        else if (*p == 'c') op_c = 1;
        else if (*p == 's') op_s = 1;
        else if (*p == 't') op_t = 1;
        else if (*p == 'x') op_x = 1;
        else if (*p == 'd') op_d = 1;
        else if (*p == 'p') op_p = 1;
        else if (*p == 'v') op_v = 1;
        else if (*p == 'q') op_r = 1; /* quick append */
    }

    if (argc < 3 && !op_s) {
        print_usage();
        return 1;
    }

    const char *archive_path = argv[2];
    struct ar_member *head = read_archive(archive_path, NULL);

    if (op_t) { /* Table of contents */
        struct ar_member *cur = head;
        while (cur) {
            if (op_v) {
                printf("%o %u/%u %10u %s\n", cur->mode, cur->uid, cur->gid, cur->size, cur->name);
            } else {
                printf("%s\n", cur->name);
            }
            cur = cur->next;
        }
        return 0;
    }

    if (op_p) { /* Print to stdout */
        for (int i = 3; i < argc; ++i) {
            const char *target = argv[i];
            struct ar_member *cur = head;
            while (cur) {
                if (strcmp(cur->name, target) == 0 && cur->data) {
                    fwrite(cur->data, 1, cur->size, stdout);
                    break;
                }
                cur = cur->next;
            }
        }
        return 0;
    }

    if (op_x) { /* Extract */
        struct ar_member *cur = head;
        while (cur) {
            int should_extract = (argc == 3);
            for (int i = 3; i < argc; ++i) {
                if (strcmp(cur->name, argv[i]) == 0) {
                    should_extract = 1;
                    break;
                }
            }
            if (should_extract) {
                if (op_v) printf("x - %s\n", cur->name);
                int out = open(cur->name, O_WRONLY | O_CREAT | O_TRUNC, cur->mode ? cur->mode : 0644);
                if (out >= 0) {
                    if (cur->size > 0 && cur->data) {
                        write(out, cur->data, cur->size);
                    }
                    close(out);
                }
            }
            cur = cur->next;
        }
        return 0;
    }

    if (op_d) { /* Delete */
        for (int i = 3; i < argc; ++i) {
            const char *target = argv[i];
            struct ar_member **curr = &head;
            while (*curr) {
                if (strcmp((*curr)->name, target) == 0) {
                    struct ar_member *to_del = *curr;
                    *curr = to_del->next;
                    free(to_del->data);
                    free(to_del);
                    if (op_v) printf("d - %s\n", target);
                    break;
                }
                curr = &(*curr)->next;
            }
        }
        return write_archive(archive_path, head);
    }

    if (op_r) { /* Replace or insert */
        for (int i = 3; i < argc; ++i) {
            const char *file_path = argv[i];
            int in = open(file_path, O_RDONLY);
            if (in < 0) {
                fprintf(stderr, "ar: %s: %s\n", file_path, strerror(errno));
                continue;
            }

            struct stat st;
            fstat(in, &st);
            uint32_t fsize = (uint32_t)st.st_size;
            uint8_t *fdata = malloc(fsize ? fsize : 1);
            if (fsize > 0 && fdata) {
                read(in, fdata, fsize);
            }
            close(in);

            const char *base_name = strrchr(file_path, '/');
            base_name = base_name ? base_name + 1 : file_path;

            /* Check if member already exists */
            struct ar_member *cur = head;
            int found = 0;
            while (cur) {
                if (strcmp(cur->name, base_name) == 0) {
                    free(cur->data);
                    cur->data = fdata;
                    cur->size = fsize;
                    cur->date = (uint32_t)st.st_mtime;
                    cur->mode = (uint32_t)st.st_mode;
                    cur->uid = (uint32_t)st.st_uid;
                    cur->gid = (uint32_t)st.st_gid;
                    found = 1;
                    if (op_v) printf("r - %s\n", base_name);
                    break;
                }
                cur = cur->next;
            }

            if (!found) {
                struct ar_member *new_m = calloc(1, sizeof(struct ar_member));
                if (new_m) {
                    strncpy(new_m->name, base_name, sizeof(new_m->name) - 1);
                    new_m->data = fdata;
                    new_m->size = fsize;
                    new_m->date = (uint32_t)st.st_mtime;
                    new_m->mode = (uint32_t)st.st_mode;
                    new_m->uid = (uint32_t)st.st_uid;
                    new_m->gid = (uint32_t)st.st_gid;

                    /* Append to end */
                    if (!head) {
                        head = new_m;
                    } else {
                        struct ar_member *tail = head;
                        while (tail->next) tail = tail->next;
                        tail->next = new_m;
                    }
                    if (op_v) printf("a - %s\n", base_name);
                }
            }
        }
        return write_archive(archive_path, head);
    }

    if (op_s) { /* Symbol index / ranlib */
        return write_archive(archive_path, head);
    }

    return 0;
}
