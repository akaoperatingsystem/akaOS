#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_VERSION "5.41 (akaOS coreutils)"

static int opt_brief = 0;       /* -b, --brief */
static int opt_mime = 0;        /* -i, --mime */
static int opt_mime_type = 0;   /* --mime-type */
static int opt_mime_enc = 0;    /* --mime-encoding */
static int opt_no_deref = 0;    /* -h, --no-dereference */
static int opt_deref = 0;       /* -L, --dereference */
static int opt_special = 0;     /* -s, --special-files */
static int opt_print0 = 0;      /* -0, --print0 */
static int opt_no_pad = 0;      /* -N, --no-pad */
static const char *opt_separator = ":";

static void print_version(void) {
    printf("file-%s\n", FILE_VERSION);
    printf("magic file from /usr/share/misc/magic\n");
}

static void print_usage(void) {
    printf("Usage: file [OPTION...] [FILE...]\n"
           "Determine type of FILEs.\n\n"
           "Options:\n"
           "  -b, --brief            do not prepend filenames to output lines\n"
           "  -i, --mime             output MIME type and encoding strings\n"
           "      --mime-type        output the MIME type\n"
           "      --mime-encoding    output the MIME encoding\n"
           "  -F, --separator STRING use string as separator instead of ':'\n"
           "  -h, --no-dereference   don't follow symlinks (default)\n"
           "  -L, --dereference      follow symlinks\n"
           "  -N, --no-pad           do not pad output filenames\n"
           "  -s, --special-files    treat block (disk) and char devices as files\n"
           "  -0, --print0           end filename with null character\n"
           "  -v, --version          output version information and exit\n"
           "      --help             display this help and exit\n");
}

/* Analyze regular file contents using header magic */
static void identify_contents(int fd, off_t size, char *desc, size_t desc_sz, char *mime_type, size_t mtype_sz, char *mime_enc, size_t menc_sz) {
    if (size == 0) {
        snprintf(desc, desc_sz, "empty");
        snprintf(mime_type, mtype_sz, "inode/x-empty");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    uint8_t buf[1024];
    ssize_t n = pread(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        snprintf(desc, desc_sz, "empty");
        snprintf(mime_type, mtype_sz, "inode/x-empty");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* ELF binary */
    if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        int is_64 = (buf[4] == 2);
        int is_dyn = (n >= 18 && (buf[16] == 3 || buf[16] == 2));
        const char *arch = "unknown";
        if (n >= 19) {
            uint16_t machine = buf[18] | (buf[19] << 8);
            if (machine == 0x3E) arch = "x86-64";
            else if (machine == 0x03) arch = "Intel 80386";
            else if (machine == 0xB7) arch = "ARM aarch64";
            else if (machine == 0x28) arch = "ARM";
            else if (machine == 0xF3) arch = "RISC-V";
        }
        snprintf(desc, desc_sz, "ELF %d-bit LSB %s, %s, version 1 (SYSV)",
                 is_64 ? 64 : 32, is_dyn ? "shared object" : "executable", arch);
        snprintf(mime_type, mtype_sz, "application/x-executable");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* Shell script with #! */
    if (n >= 2 && buf[0] == '#' && buf[1] == '!') {
        char interp[64] = "";
        ssize_t i = 2;
        while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
        ssize_t start = i;
        while (i < n && buf[i] != '\n' && buf[i] != '\r' && buf[i] != ' ' && (size_t)(i - start) < sizeof(interp) - 1) {
            interp[i - start] = (char)buf[i];
            i++;
        }
        interp[i - start] = '\0';
        snprintf(desc, desc_sz, "%s script text executable", interp[0] ? interp : "shell");
        snprintf(mime_type, mtype_sz, "text/x-shellscript");
        snprintf(mime_enc, menc_sz, "us-ascii");
        return;
    }

    /* x86 boot sector (MBR) */
    if (n >= 512 && buf[510] == 0x55 && buf[511] == 0xAA) {
        snprintf(desc, desc_sz, "DOS/MBR boot sector; partition 1");
        snprintf(mime_type, mtype_sz, "application/x-dosexec");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* Tar archive */
    if (n >= 262 && memcmp(buf + 257, "ustar", 5) == 0) {
        snprintf(desc, desc_sz, "POSIX tar archive (ustar)");
        snprintf(mime_type, mtype_sz, "application/x-tar");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* Gzip compressed */
    if (n >= 2 && buf[0] == 0x1F && buf[1] == 0x8B) {
        snprintf(desc, desc_sz, "gzip compressed data");
        snprintf(mime_type, mtype_sz, "application/gzip");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* Bzip2 compressed */
    if (n >= 3 && buf[0] == 'B' && buf[1] == 'Z' && buf[2] == 'h') {
        snprintf(desc, desc_sz, "bzip2 compressed data");
        snprintf(mime_type, mtype_sz, "application/x-bzip2");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* PNG image */
    if (n >= 8 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0) {
        snprintf(desc, desc_sz, "PNG image data");
        snprintf(mime_type, mtype_sz, "image/png");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* JPEG image */
    if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
        snprintf(desc, desc_sz, "JPEG image data");
        snprintf(mime_type, mtype_sz, "image/jpeg");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* ASCII / UTF-8 or Binary text heuristic */
    int is_ascii = 1;
    int is_utf8 = 1;
    int has_null = 0;
    for (ssize_t i = 0; i < n; ++i) {
        uint8_t c = buf[i];
        if (c == 0) {
            has_null = 1;
            break;
        }
        if (c < 32 && c != '\t' && c != '\n' && c != '\r' && c != 12 && c != 27) {
            is_ascii = 0;
            is_utf8 = 0;
            break;
        }
        if (c > 127) {
            is_ascii = 0;
        }
    }

    if (has_null) {
        snprintf(desc, desc_sz, "data");
        snprintf(mime_type, mtype_sz, "application/octet-stream");
        snprintf(mime_enc, menc_sz, "binary");
        return;
    }

    /* C source file heuristic */
    if (strstr((const char *)buf, "#include") != NULL ||
        strstr((const char *)buf, "int main(") != NULL ||
        strstr((const char *)buf, "void ") != NULL) {
        snprintf(desc, desc_sz, "C source, %s text", is_ascii ? "ASCII" : "UTF-8");
        snprintf(mime_type, mtype_sz, "text/x-c");
        snprintf(mime_enc, menc_sz, is_ascii ? "us-ascii" : "utf-8");
        return;
    }

    if (is_ascii) {
        snprintf(desc, desc_sz, "ASCII text");
        snprintf(mime_type, mtype_sz, "text/plain");
        snprintf(mime_enc, menc_sz, "us-ascii");
    } else if (is_utf8) {
        snprintf(desc, desc_sz, "UTF-8 Unicode text");
        snprintf(mime_type, mtype_sz, "text/plain");
        snprintf(mime_enc, menc_sz, "utf-8");
    } else {
        snprintf(desc, desc_sz, "data");
        snprintf(mime_type, mtype_sz, "application/octet-stream");
        snprintf(mime_enc, menc_sz, "binary");
    }
}

static int process_file(const char *path) {
    struct stat st;
    int res = (opt_deref && !opt_no_deref) ? stat(path, &st) : lstat(path, &st);
    if (res != 0) {
        fprintf(stderr, "file: cannot open '%s' (%s)\n", path, strerror(errno));
        return 0; /* POSIX file: cannot open is not fatal exit error */
    }

    char desc[256] = "";
    char mime_type[128] = "";
    char mime_enc[64] = "";

    if (S_ISDIR(st.st_mode)) {
        snprintf(desc, sizeof(desc), "directory");
        snprintf(mime_type, sizeof(mime_type), "inode/directory");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    } else if (S_ISLNK(st.st_mode)) {
        char link_target[256];
        ssize_t len = readlink(path, link_target, sizeof(link_target) - 1);
        if (len > 0) link_target[len] = '\0';
        else strcpy(link_target, "broken");
        snprintf(desc, sizeof(desc), "symbolic link to %s", link_target);
        snprintf(mime_type, sizeof(mime_type), "inode/symlink");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    } else if (S_ISCHR(st.st_mode)) {
        snprintf(desc, sizeof(desc), "character special (%u/%u)",
                 (unsigned)(st.st_rdev >> 8), (unsigned)(st.st_rdev & 0xFF));
        snprintf(mime_type, sizeof(mime_type), "inode/chardevice");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    } else if (S_ISBLK(st.st_mode)) {
        snprintf(desc, sizeof(desc), "block special (%u/%u)",
                 (unsigned)(st.st_rdev >> 8), (unsigned)(st.st_rdev & 0xFF));
        snprintf(mime_type, sizeof(mime_type), "inode/blockdevice");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    } else if (S_ISFIFO(st.st_mode)) {
        snprintf(desc, sizeof(desc), "fifo (named pipe)");
        snprintf(mime_type, sizeof(mime_type), "inode/fifo");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    } else if (S_ISSOCK(st.st_mode)) {
        snprintf(desc, sizeof(desc), "socket");
        snprintf(mime_type, sizeof(mime_type), "inode/socket");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    } else if (S_ISREG(st.st_mode)) {
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            identify_contents(fd, st.st_size, desc, sizeof(desc), mime_type, sizeof(mime_type), mime_enc, sizeof(mime_enc));
            close(fd);
        } else {
            snprintf(desc, sizeof(desc), "unreadable");
            snprintf(mime_type, sizeof(mime_type), "application/octet-stream");
            snprintf(mime_enc, sizeof(mime_enc), "binary");
        }
    } else {
        snprintf(desc, sizeof(desc), "special");
        snprintf(mime_type, sizeof(mime_type), "application/octet-stream");
        snprintf(mime_enc, sizeof(mime_enc), "binary");
    }

    /* Print result */
    if (!opt_brief) {
        if (opt_print0) {
            printf("%s%c", path, '\0');
        } else {
            printf("%s%s ", path, opt_separator);
        }
    }

    if (opt_mime) {
        printf("%s; charset=%s\n", mime_type, mime_enc);
    } else if (opt_mime_type) {
        printf("%s\n", mime_type);
    } else if (opt_mime_enc) {
        printf("%s\n", mime_enc);
    } else {
        printf("%s\n", desc);
    }

    return 0;
}

int main(int argc, char **argv) {
    char *files[256];
    int file_count = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-b") == 0 || strcmp(arg, "--brief") == 0) {
            opt_brief = 1;
            continue;
        }
        if (strcmp(arg, "-i") == 0 || strcmp(arg, "--mime") == 0) {
            opt_mime = 1;
            continue;
        }
        if (strcmp(arg, "--mime-type") == 0) {
            opt_mime_type = 1;
            continue;
        }
        if (strcmp(arg, "--mime-encoding") == 0) {
            opt_mime_enc = 1;
            continue;
        }
        if (strcmp(arg, "-L") == 0 || strcmp(arg, "--dereference") == 0) {
            opt_deref = 1;
            opt_no_deref = 0;
            continue;
        }
        if (strcmp(arg, "--no-dereference") == 0) {
            opt_no_deref = 1;
            opt_deref = 0;
            continue;
        }
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--special-files") == 0) {
            opt_special = 1;
            continue;
        }
        if (strcmp(arg, "-0") == 0 || strcmp(arg, "--print0") == 0) {
            opt_print0 = 1;
            continue;
        }
        if (strcmp(arg, "-N") == 0 || strcmp(arg, "--no-pad") == 0) {
            opt_no_pad = 1;
            continue;
        }
        if (strcmp(arg, "-F") == 0 && i + 1 < argc) {
            opt_separator = argv[++i];
            continue;
        }
        if (strncmp(arg, "-F", 2) == 0 && strlen(arg) > 2) {
            opt_separator = arg + 2;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            for (int j = i + 1; j < argc && file_count < 256; ++j) {
                files[file_count++] = argv[j];
            }
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            /* Support clustered flags */
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'b') opt_brief = 1;
                else if (arg[c] == 'i') opt_mime = 1;
                else if (arg[c] == 'L') { opt_deref = 1; opt_no_deref = 0; }
                else if (arg[c] == 'h') { opt_no_deref = 1; opt_deref = 0; }
                else if (arg[c] == 's') opt_special = 1;
                else if (arg[c] == 'N') opt_no_pad = 1;
                else if (arg[c] == '0') opt_print0 = 1;
                else {
                    /* Ignore other flags safely */
                }
            }
            continue;
        }
        if (file_count < 256) {
            files[file_count++] = arg;
        }
    }

    if (file_count == 0) {
        print_usage();
        return 1;
    }

    int ret = 0;
    for (int i = 0; i < file_count; ++i) {
        if (process_file(files[i]) != 0) {
            ret = 1;
        }
    }
    return ret;
}
