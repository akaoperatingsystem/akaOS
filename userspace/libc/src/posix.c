#include <akaos/architecture.h>
#include <akaos/errno.h>
#include <akaos/version.h>
#include <akaos/syscall.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <glob.h>
#include <iconv.h>

struct __akaos_DIR {
    int descriptor;
    size_t count;
    size_t index;
    struct akaos_dirent native_entries[16];
    struct dirent current_entry;
};

static char akaos_locale_name[16] = "C.UTF-8";
static struct lconv akaos_locale_format;
static int akaos_locale_format_ready;
struct __akaos_locale {
    const char *name;
    size_t max_bytes;
};
static struct __akaos_locale akaos_locale_c = {"C", 1U};
static struct __akaos_locale akaos_locale_utf8 = {"C.UTF-8", 4U};
static locale_t akaos_global_locale = &akaos_locale_utf8;
static __thread locale_t akaos_thread_locale;

static locale_t locale_from_name(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0)
        return &akaos_locale_c;
    if (strcmp(name, "C.UTF-8") == 0 || strcmp(name, "C.utf8") == 0 ||
        strcmp(name, "UTF-8") == 0)
        return &akaos_locale_utf8;
    return NULL;
}

char *setlocale(int category, const char *locale) {
    if (category != LC_ALL && category != LC_COLLATE &&
        category != LC_CTYPE && category != LC_MONETARY &&
        category != LC_MESSAGES && category != LC_NUMERIC &&
        category != LC_TIME) {
        return NULL;
    }
    if (!locale) return akaos_locale_name;
    const char *requested = locale;
    if (*requested == '\0') {
        requested = getenv("LC_ALL");
        if (!requested || !*requested) requested = getenv("LANG");
        if (!requested || !*requested) requested = "C.UTF-8";
    }
    locale_t selected = locale_from_name(requested);
    if (!selected) return NULL;
    strncpy(akaos_locale_name, requested, sizeof(akaos_locale_name) - 1U);
    akaos_locale_name[sizeof(akaos_locale_name) - 1U] = '\0';
    akaos_global_locale = selected;
    akaos_locale_format_ready = 0;
    return akaos_locale_name;
}

locale_t newlocale(int category_mask, const char *locale, locale_t base) {
    if (!locale || category_mask == 0 ||
        (category_mask & ~LC_ALL_MASK) != 0) {
        errno = EINVAL;
        return NULL;
    }
    locale_t selected = locale_from_name(locale);
    if (!selected) {
        errno = EINVAL;
        return NULL;
    }
    (void)base;
    return selected;
}

locale_t duplocale(locale_t locale) {
    if (locale == LC_GLOBAL_LOCALE) return akaos_global_locale;
    if (locale == &akaos_locale_c || locale == &akaos_locale_utf8) return locale;
    errno = EINVAL;
    return NULL;
}

void freelocale(locale_t locale) {
    /* These immutable built-in objects have static storage and are shared. */
    (void)locale;
}

locale_t uselocale(locale_t locale) {
    locale_t previous = akaos_thread_locale ? akaos_thread_locale :
                        LC_GLOBAL_LOCALE;
    if (!locale) return previous;
    if (locale == LC_GLOBAL_LOCALE) {
        akaos_thread_locale = NULL;
        return previous;
    }
    if (locale != &akaos_locale_c && locale != &akaos_locale_utf8) {
        errno = EINVAL;
        return NULL;
    }
    akaos_thread_locale = locale;
    return previous;
}

size_t __akaos_mb_cur_max(void) {
    locale_t locale = akaos_thread_locale ? akaos_thread_locale :
                      akaos_global_locale;
    return locale ? locale->max_bytes : 1U;
}

struct lconv *localeconv(void) {
    if (!akaos_locale_format_ready) {
        memset(&akaos_locale_format, 0, sizeof(akaos_locale_format));
        akaos_locale_format.decimal_point = ".";
        akaos_locale_format.thousands_sep = "";
        akaos_locale_format.grouping = "";
        akaos_locale_format.int_curr_symbol = "";
        akaos_locale_format.currency_symbol = "";
        akaos_locale_format.mon_decimal_point = "";
        akaos_locale_format.mon_thousands_sep = "";
        akaos_locale_format.mon_grouping = "";
        akaos_locale_format.positive_sign = "";
        akaos_locale_format.negative_sign = "-";
        akaos_locale_format.int_frac_digits = (char)-1;
        akaos_locale_format.frac_digits = (char)-1;
        akaos_locale_format.p_cs_precedes = (char)-1;
        akaos_locale_format.p_sep_by_space = (char)-1;
        akaos_locale_format.n_cs_precedes = (char)-1;
        akaos_locale_format.n_sep_by_space = (char)-1;
        akaos_locale_format.p_sign_posn = (char)-1;
        akaos_locale_format.n_sign_posn = (char)-1;
        akaos_locale_format.int_p_cs_precedes = (char)-1;
        akaos_locale_format.int_p_sep_by_space = (char)-1;
        akaos_locale_format.int_n_cs_precedes = (char)-1;
        akaos_locale_format.int_n_sep_by_space = (char)-1;
        akaos_locale_format.int_p_sign_posn = (char)-1;
        akaos_locale_format.int_n_sign_posn = (char)-1;
        akaos_locale_format_ready = 1;
    }
    return &akaos_locale_format;
}

char *nl_langinfo_l(nl_item item, locale_t locale) {
    static char empty[] = "";
    if (locale == LC_GLOBAL_LOCALE) {
        locale = akaos_thread_locale ? akaos_thread_locale :
                 akaos_global_locale;
    }
    if (locale != &akaos_locale_c && locale != &akaos_locale_utf8) {
        errno = EINVAL;
        return empty;
    }
    switch (item) {
        case CODESET:
            return locale == &akaos_locale_utf8 ? "UTF-8" : "US-ASCII";
        case D_T_FMT:
            return "%a %b %e %H:%M:%S %Y";
        case D_FMT:
            return "%m/%d/%y";
        case T_FMT:
            return "%H:%M:%S";
        case T_FMT_AMPM:
            return "%I:%M:%S %p";
        case AM_STR:
            return "AM";
        case PM_STR:
            return "PM";
        case RADIXCHAR:
            return localeconv()->decimal_point;
        case THOUSEP:
            return localeconv()->thousands_sep;
        case YESEXPR:
            return "^[yY]";
        case NOEXPR:
            return "^[nN]";
        default:
            errno = EINVAL;
            return empty;
    }
}

char *nl_langinfo(nl_item item) {
    return nl_langinfo_l(item, LC_GLOBAL_LOCALE);
}

int access(const char *path, int mode) {
    if (!path || (mode & ~(R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    struct akaos_stat status;
    if (stat(path, &status) < 0) return -1;
    /* Permission enforcement is currently kernel-owned and root is the
     * default interactive account.  Existence is therefore the only safe
     * check exposed by this first POSIX tranche. */
    (void)status;
    return 0;
}

int gethostname(char *name, size_t length) {
    if (!name || length == 0) {
        errno = EINVAL;
        return -1;
    }
    char hostname[64] = "akaOS";
    int descriptor = open("/etc/akaos.conf", O_RDONLY);
    if (descriptor >= 0) {
        char config[512];
        ssize_t count = read(descriptor, config, sizeof(config) - 1U);
        (void)close(descriptor);
        if (count > 0) {
            config[count] = '\0';
            const char *entry = strstr(config, "HOSTNAME=");
            if (entry) {
                entry += 9;
                size_t index = 0;
                while (entry[index] && entry[index] != '\n' &&
                       entry[index] != '\r' && index < sizeof(hostname) - 1U) {
                    hostname[index] = entry[index];
                    ++index;
                }
                hostname[index] = '\0';
            }
        }
    }
    size_t copy = strlen(hostname);
    if (copy >= length) {
        copy = length - 1U;
        errno = ENAMETOOLONG;
        memcpy(name, hostname, copy);
        name[copy] = '\0';
        return -1;
    }
    memcpy(name, hostname, copy + 1U);
    return 0;
}

long sysconf(int name) {
    switch (name) {
        case _SC_ARG_MAX: return 4096;
        case _SC_CHILD_MAX: return 16;
        case _SC_CLK_TCK: return 100;
        case _SC_OPEN_MAX: return 1024;
        case _SC_GETGR_R_SIZE_MAX: return 1024;
        case _SC_GETPW_R_SIZE_MAX: return 1024;
        case _SC_PAGESIZE: return 4096;
        case _SC_NPROCESSORS_ONLN: return 1;
        case _SC_PHYS_PAGES:
        case _SC_AVPHYS_PAGES: return 262144;
        default:
            errno = EINVAL;
            return -1;
    }
}

long pathconf(const char *path, int name) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    struct stat status;
    if (stat(path, &status) < 0) return -1;
    switch (name) {
        case _PC_PATH_MAX: return AKAOS_SYSCALL_PATH_MAX;
        case _PC_NAME_MAX: return AKAOS_NAME_MAX;
        case _PC_LINK_MAX:
            /* akaFS nodes have a single name, so a file never has more than
             * one hard link.  Reporting this honestly lets callers such as
             * libXau pick a locking strategy that does not need links. */
            return 1;
        case _PC_PIPE_BUF: return 4096;
        case _PC_SYMLINK_MAX: return AKAOS_NAME_MAX;
        case _PC_REC_MAX_XFER: return 131072;
        case _PC_CHOWN_RESTRICTED: return 0;
        case _PC_NO_TRUNC: return 1;
        case _PC_VDISABLE: return 0;
        case _PC_MAX_CANON: return 1024;
        case _PC_MAX_INPUT: return 1024;
        case _PC_2_SYMLINKS: return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int uname(struct utsname *name) {
    if (!name) {
        errno = EFAULT;
        return -1;
    }
    memset(name, 0, sizeof(*name));
    strncpy(name->sysname, AKAOS_NAME, sizeof(name->sysname) - 1U);
    strncpy(name->nodename, "akaOS", sizeof(name->nodename) - 1U);
    strncpy(name->release, AKAOS_VERSION, sizeof(name->release) - 1U);
    strncpy(name->version, XSCORE_VERSION_STRING, sizeof(name->version) - 1U);
    strncpy(name->machine, AKAOS_ARCH_NAME, sizeof(name->machine) - 1U);
    return 0;
}

DIR *fdopendir(int descriptor) {
    if (descriptor < 0) {
        errno = EBADF;
        return NULL;
    }
    struct stat status;
    if (fstat(descriptor, &status) < 0) return NULL;
    if (!S_ISDIR(status.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    DIR *directory = (DIR *)calloc(1U, sizeof(*directory));
    if (!directory) return NULL;
    directory->descriptor = descriptor;
    return directory;
}

DIR *opendir(const char *path) {
    if (!path) {
        errno = EFAULT;
        return NULL;
    }
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return NULL;
    DIR *directory = fdopendir(descriptor);
    if (!directory) {
        int saved_errno = errno;
        (void)close(descriptor);
        errno = saved_errno;
    }
    return directory;
}

struct dirent *readdir(DIR *directory) {
    if (!directory) {
        errno = EBADF;
        return NULL;
    }
    if (directory->index >= directory->count) {
        errno = 0;
        ssize_t count = getdents(directory->descriptor,
                                 directory->native_entries,
                                 sizeof(directory->native_entries) /
                                     sizeof(directory->native_entries[0]));
        if (count <= 0) return NULL;
        directory->count = (size_t)count;
        directory->index = 0U;
    }
    const struct akaos_dirent *native_entry =
        &directory->native_entries[directory->index];
    memset(&directory->current_entry, 0, sizeof(directory->current_entry));
    directory->current_entry.d_ino = (ino_t)native_entry->inode;
    directory->current_entry.d_off = (off_t)(directory->index + 1U);
    directory->current_entry.d_reclen = (unsigned short)sizeof(struct dirent);
    switch (native_entry->type) {
        case AKAOS_DT_DIRECTORY: directory->current_entry.d_type = DT_DIR; break;
        case AKAOS_DT_REGULAR: directory->current_entry.d_type = DT_REG; break;
        case AKAOS_DT_CHARACTER: directory->current_entry.d_type = DT_CHR; break;
        default: directory->current_entry.d_type = DT_UNKNOWN; break;
    }
    size_t name_length = 0U;
    while (name_length < AKAOS_NAME_MAX && native_entry->name[name_length])
        ++name_length;
    memcpy(directory->current_entry.d_name, native_entry->name, name_length);
    directory->current_entry.d_name[name_length] = '\0';
    ++directory->index;
    return &directory->current_entry;
}

static mode_t process_umask = 0022;

mode_t umask(mode_t mask) {
    mode_t previous = process_umask;
    process_umask = mask & 0777;
    return previous;
}

int daemon(int nochdir, int noclose) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    if (setsid() < 0) return -1;

    if (!nochdir) {
        (void)chdir("/");
    }

    if (!noclose) {
        int fd = open("/dev/null", O_RDWR, 0);
        if (fd >= 0) {
            (void)dup2(fd, STDIN_FILENO);
            (void)dup2(fd, STDOUT_FILENO);
            (void)dup2(fd, STDERR_FILENO);
            if (fd > 2) (void)close(fd);
        }
    }
    return 0;
}

int closedir(DIR *directory) {
    if (!directory) {
        errno = EBADF;
        return -1;
    }
    int result = close(directory->descriptor);
    int saved_errno = errno;
    free(directory);
    errno = saved_errno;
    return result;
}

void rewinddir(DIR *directory) {
    if (!directory) {
        errno = EBADF;
        return;
    }
    (void)lseek(directory->descriptor, 0, SEEK_SET);
    directory->count = 0U;
    directory->index = 0U;
}

long telldir(DIR *directory) {
    if (!directory) {
        errno = EBADF;
        return -1L;
    }
    off_t position = lseek(directory->descriptor, 0, SEEK_CUR);
    if (position < 0) return -1L;
    off_t unread = (off_t)(directory->count - directory->index);
    off_t logical_position = position - unread;
    if ((off_t)(long)logical_position != logical_position) {
        errno = EOVERFLOW;
        return -1L;
    }
    return (long)logical_position;
}

void seekdir(DIR *directory, long location) {
    if (!directory || location < 0) {
        errno = EINVAL;
        return;
    }
    if (lseek(directory->descriptor, (off_t)location, SEEK_SET) < 0) return;
    directory->count = 0U;
    directory->index = 0U;
}

int dirfd(DIR *directory) {
    if (!directory) {
        errno = EBADF;
        return -1;
    }
    return directory->descriptor;
}

int alphasort(const struct dirent **a, const struct dirent **b) {
    if (!a || !*a) return (b && *b) ? -1 : 0;
    if (!b || !*b) return 1;
    return strcoll((*a)->d_name, (*b)->d_name);
}

int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
    if (!dirp || !namelist) {
        errno = EINVAL;
        return -1;
    }
    DIR *d = opendir(dirp);
    if (!d) return -1;
    struct dirent **list = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (filter && !filter(entry)) continue;
        if (count >= capacity) {
            size_t new_cap = capacity ? capacity * 2 : 16;
            struct dirent **new_list = (struct dirent **)realloc(list, new_cap * sizeof(*list));
            if (!new_list) goto err;
            list = new_list;
            capacity = new_cap;
        }
        struct dirent *copy = (struct dirent *)malloc(sizeof(struct dirent));
        if (!copy) goto err;
        memcpy(copy, entry, sizeof(struct dirent));
        list[count++] = copy;
    }
    closedir(d);
    if (compar && count > 1) {
        qsort(list, count, sizeof(struct dirent *), (int (*)(const void *, const void *))compar);
    }
    *namelist = list;
    return (int)count;

err: {
        int saved_errno = errno;
        closedir(d);
        if (list) {
            for (size_t i = 0; i < count; i++) free(list[i]);
            free(list);
        }
        errno = saved_errno;
        return -1;
    }
}

int scandir64(const char *dirp, struct dirent ***namelist,
              int (*filter)(const struct dirent *),
              int (*compar)(const struct dirent **, const struct dirent **)) {
    return scandir(dirp, namelist, filter, compar);
}

int alphasort64(const struct dirent **a, const struct dirent **b) {
    return alphasort(a, b);
}

int fchmod(int descriptor, mode_t mode) {
    (void)descriptor;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int getrlimit(int resource, struct rlimit *limit) {
    if (!limit) {
        errno = EFAULT;
        return -1;
    }
    if (resource == RLIMIT_NOFILE) {
        long descriptor_limit = sysconf(_SC_OPEN_MAX);
        if (descriptor_limit <= 0) descriptor_limit = 1024;
        limit->rlim_cur = (rlim_t)descriptor_limit;
        limit->rlim_max = (rlim_t)descriptor_limit;
        return 0;
    }
    if (resource == RLIMIT_CORE) {
        /* XSCore does not generate core files. */
        limit->rlim_cur = 0;
        limit->rlim_max = 0;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int setrlimit(int resource, const struct rlimit *limit) {
    if (!limit) {
        errno = EFAULT;
        return -1;
    }
    if (limit->rlim_cur > limit->rlim_max) {
        errno = EINVAL;
        return -1;
    }
    if (resource == RLIMIT_CORE && limit->rlim_cur == 0)
        return 0;
    if (resource == RLIMIT_NOFILE) {
        long descriptor_limit = sysconf(_SC_OPEN_MAX);
        if (descriptor_limit <= 0) descriptor_limit = 1024;
        if (limit->rlim_cur == (rlim_t)descriptor_limit &&
            limit->rlim_max == (rlim_t)descriptor_limit)
            return 0;
        errno = EOPNOTSUPP;
        return -1;
    }
    errno = resource == RLIMIT_CORE ? EOPNOTSUPP : EINVAL;
    return -1;
}

int fchmodat(int directory, const char *path, mode_t mode, int flags) {
    if (!path || (flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (path[0] != '/' && directory != AT_FDCWD) {
        errno = ENOSYS;
        return -1;
    }
    /* No symlink vnodes exist yet, so NOFOLLOW and normal path chmod coincide. */
    return chmod(path, mode);
}

int link(const char *oldpath, const char *newpath) {
    return linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

int linkat(int old_directory, const char *oldpath, int new_directory,
           const char *newpath, int flags) {
    (void)old_directory;
    (void)new_directory;
    if (!oldpath || !newpath) {
        errno = EFAULT;
        return -1;
    }
    if ((flags & ~AT_SYMLINK_FOLLOW) != 0) {
        errno = EINVAL;
        return -1;
    }
    size_t old_length = strlen(oldpath);
    size_t new_length = strlen(newpath);
    if (old_length == 0U || new_length == 0U) {
        errno = ENOENT;
        return -1;
    }
    if (old_length > AKAOS_SYSCALL_PATH_MAX ||
        new_length > AKAOS_SYSCALL_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    /* The current VFS has no hard-link inode/reference-count model. Report a
     * filesystem capability failure so callers can use copy/rename fallback. */
    errno = EPERM;
    return -1;
}

int symlink(const char *target, const char *linkpath) {
    (void)target;
    (void)linkpath;
    errno = ENOSYS;
    return -1;
}

ssize_t readlink(const char *path, char *buffer, size_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    errno = ENOSYS;
    return -1;
}

int truncate(const char *path, off_t length) {
    if (!path || length < 0) {
        errno = EINVAL;
        return -1;
    }
    int descriptor = open(path, O_WRONLY);
    if (descriptor < 0) return -1;
    int result = ftruncate(descriptor, length);
    int saved_errno = errno;
    (void)close(descriptor);
    errno = saved_errno;
    return result;
}

char *realpath(const char *path, char *resolved_path) {
    if (!path || !*path) {
        errno = EINVAL;
        return NULL;
    }
    char candidate[256];
    size_t input_length = strlen(path);
    if (input_length >= sizeof(candidate)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    if (path[0] == '/') {
        memcpy(candidate, path, input_length + 1U);
    } else {
        if (!getcwd(candidate, sizeof(candidate))) return NULL;
        size_t base_length = strlen(candidate);
        if (base_length + 1U + input_length >= sizeof(candidate)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        if (base_length == 0U || candidate[base_length - 1U] != '/')
            candidate[base_length++] = '/';
        memcpy(candidate + base_length, path, input_length + 1U);
    }

    char normalized[256] = "/";
    size_t normalized_length = 1U;
    size_t segment_starts[64];
    size_t segment_count = 0U;
    const char *cursor = candidate;
    while (*cursor) {
        while (*cursor == '/') ++cursor;
        const char *segment = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        size_t segment_length = (size_t)(cursor - segment);
        if (segment_length == 0U) break;
        if (segment_length == 1U && segment[0] == '.') continue;
        if (segment_length == 2U && segment[0] == '.' && segment[1] == '.') {
            if (segment_count > 0U) {
                normalized_length = segment_starts[--segment_count];
                normalized[normalized_length] = '\0';
            }
            continue;
        }
        if (segment_count >= sizeof(segment_starts) / sizeof(segment_starts[0]) ||
            normalized_length + segment_length + 1U >= sizeof(normalized)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        if (normalized_length > 1U) normalized[normalized_length++] = '/';
        segment_starts[segment_count++] = normalized_length > 1U
                                              ? normalized_length - 1U
                                              : 1U;
        memcpy(normalized + normalized_length, segment, segment_length);
        normalized_length += segment_length;
        normalized[normalized_length] = '\0';
    }

    struct stat status;
    if (stat(normalized, &status) < 0) return NULL;
    if (resolved_path) {
        memcpy(resolved_path, normalized, normalized_length + 1U);
        return resolved_path;
    }
    char *allocated = (char *)malloc(normalized_length + 1U);
    if (!allocated) return NULL;
    memcpy(allocated, normalized, normalized_length + 1U);
    return allocated;
}

struct akaos_wordexp_word {
    char *text;
    unsigned char *quoted;
    size_t length;
    size_t capacity;
    int started;
};

struct akaos_wordexp_words {
    char **items;
    unsigned char **quoted;
    size_t count;
    size_t capacity;
};

struct akaos_wordexp_paths {
    char **items;
    size_t count;
    size_t capacity;
};

static int akaos_wordexp_words_reserve(struct akaos_wordexp_words *words,
                                       size_t additional) {
    if (additional > SIZE_MAX - words->count) return 0;
    size_t needed = words->count + additional;
    if (needed <= words->capacity) return 1;
    size_t capacity = words->capacity ? words->capacity : 8U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) return 0;
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*words->items)) return 0;
    char **items = (char **)realloc(words->items,
                                    capacity * sizeof(*words->items));
    if (!items) return 0;
    words->items = items;
    unsigned char **quoted = (unsigned char **)realloc(
        words->quoted, capacity * sizeof(*words->quoted));
    if (!quoted) return 0;
    words->quoted = quoted;
    words->capacity = capacity;
    return 1;
}

static int akaos_wordexp_words_push_owned(struct akaos_wordexp_words *words,
                                          char *word,
                                          unsigned char *quoted) {
    if (!akaos_wordexp_words_reserve(words, 1U)) return 0;
    words->items[words->count++] = word;
    words->quoted[words->count - 1U] = quoted;
    return 1;
}

static int akaos_wordexp_words_push_copy(struct akaos_wordexp_words *words,
                                         const char *word) {
    size_t length = strlen(word);
    char *copy = (char *)malloc(length + 1U);
    if (!copy) return 0;
    memcpy(copy, word, length + 1U);
    unsigned char *quoted = (unsigned char *)calloc(length + 1U, 1U);
    if (!quoted || !akaos_wordexp_words_push_owned(words, copy, quoted)) {
        free(quoted);
        free(copy);
        return 0;
    }
    return 1;
}

static void akaos_wordexp_words_destroy(struct akaos_wordexp_words *words) {
    if (!words) return;
    for (size_t index = 0; index < words->count; ++index) {
        free(words->items[index]);
        free(words->quoted[index]);
    }
    free(words->items);
    free(words->quoted);
    words->items = NULL;
    words->count = 0U;
    words->capacity = 0U;
}

static int akaos_wordexp_word_reserve(struct akaos_wordexp_word *word,
                                      size_t additional) {
    if (additional > SIZE_MAX - word->length - 1U) return 0;
    size_t needed = word->length + additional + 1U;
    if (needed <= word->capacity) return 1;
    size_t capacity = word->capacity ? word->capacity : 32U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) return 0;
        capacity *= 2U;
    }
    char *text = (char *)realloc(word->text, capacity);
    if (!text) return 0;
    word->text = text;
    unsigned char *quoted = (unsigned char *)realloc(word->quoted, capacity);
    if (!quoted) return 0;
    word->quoted = quoted;
    word->capacity = capacity;
    return 1;
}

static int akaos_wordexp_word_append(struct akaos_wordexp_word *word,
                                     char character, int quoted) {
    if (!akaos_wordexp_word_reserve(word, 1U)) return 0;
    word->text[word->length] = character;
    word->quoted[word->length] = quoted ? 1U : 0U;
    ++word->length;
    word->text[word->length] = '\0';
    word->started = 1;
    return 1;
}

static int akaos_wordexp_word_append_text(struct akaos_wordexp_word *word,
                                          const char *text, int quoted) {
    if (!text) return 1;
    size_t length = strlen(text);
    if (!akaos_wordexp_word_reserve(word, length)) return 0;
    for (size_t index = 0; index < length; ++index) {
        word->text[word->length] = text[index];
        word->quoted[word->length] = quoted ? 1U : 0U;
        ++word->length;
    }
    word->text[word->length] = '\0';
    if (length != 0U) word->started = 1;
    return 1;
}

static int akaos_wordexp_word_finish(struct akaos_wordexp_word *word,
                                     struct akaos_wordexp_words *words) {
    if (!word->started) return 1;
    if (!word->text) {
        word->text = (char *)malloc(1U);
        if (!word->text) return 0;
        word->text[0] = '\0';
    } else {
        word->text[word->length] = '\0';
    }
    if (!word->quoted) {
        word->quoted = (unsigned char *)calloc(word->length + 1U, 1U);
        if (!word->quoted) return 0;
    }
    if (!akaos_wordexp_words_push_owned(words, word->text, word->quoted))
        return 0;
    word->text = NULL;
    word->quoted = NULL;
    word->length = 0U;
    word->capacity = 0U;
    word->started = 0;
    return 1;
}

static void akaos_wordexp_word_destroy(struct akaos_wordexp_word *word) {
    free(word->text);
    free(word->quoted);
    memset(word, 0, sizeof(*word));
}

static int akaos_wordexp_is_ifs(char character, const char *ifs) {
    return ifs && strchr(ifs, character) != NULL;
}

static int akaos_wordexp_append_expansion(
    struct akaos_wordexp_word *word, struct akaos_wordexp_words *words,
    const char *value, const char *ifs, int quoted) {
    if (!value) value = "";
    if (quoted) return akaos_wordexp_word_append_text(word, value, 1);

    for (size_t index = 0; value[index]; ++index) {
        if (akaos_wordexp_is_ifs(value[index], ifs)) {
            if (!akaos_wordexp_word_finish(word, words)) return 0;
            while (value[index + 1U] &&
                   akaos_wordexp_is_ifs(value[index + 1U], ifs)) {
                ++index;
            }
        } else if (!akaos_wordexp_word_append(word, value[index], 0)) {
            return 0;
        }
    }
    return 1;
}

static int akaos_wordexp_is_name_start(char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') || character == '_';
}

static int akaos_wordexp_is_name_character(char character) {
    return akaos_wordexp_is_name_start(character) ||
           (character >= '0' && character <= '9');
}

static int akaos_wordexp_append_parameter(
    const char *input, size_t *cursor, struct akaos_wordexp_word *word,
    struct akaos_wordexp_words *words, const char *ifs, int flags,
    int quoted) {
    size_t position = *cursor + 1U;
    if (input[position] == '(' || input[position] == '`')
        return WRDE_CMDSUB;

    char name[PATH_MAX];
    size_t name_length = 0U;
    int length_operator = 0;
    char operator_character = '\0';
    int operator_colon = 0;
    const char *fallback = NULL;
    size_t fallback_length = 0U;
    size_t after = position;
    if (input[position] == '{') {
        size_t start = position + 1U;
        size_t end = start;
        while (input[end] && input[end] != '}') {
            if (input[end] == '$' && input[end + 1U] == '(')
                return WRDE_CMDSUB;
            ++end;
        }
        if (!input[end]) return WRDE_SYNTAX;
        if (input[start] == '#') {
            length_operator = 1;
            ++start;
        }
        size_t name_end = start;
        if (akaos_wordexp_is_name_start(input[name_end])) {
            ++name_end;
            while (akaos_wordexp_is_name_character(input[name_end]))
                ++name_end;
        } else if (!length_operator) {
            return WRDE_SYNTAX;
        }
        name_length = name_end - start;
        if (name_length >= sizeof(name)) return WRDE_SYNTAX;
        memcpy(name, input + start, name_length);
        name[name_length] = '\0';
        if (name_end < end) {
            if (input[name_end] == ':') {
                operator_colon = 1;
                ++name_end;
            }
            operator_character = input[name_end++];
            if (operator_character != '-' && operator_character != '+' &&
                operator_character != '?' && operator_character != '=') {
                return WRDE_SYNTAX;
            }
            fallback = input + name_end;
            fallback_length = end - name_end;
        }
        after = end + 1U;
    } else if (akaos_wordexp_is_name_start(input[position])) {
        size_t end = position + 1U;
        while (akaos_wordexp_is_name_character(input[end])) ++end;
        name_length = end - position;
        if (name_length >= sizeof(name)) return WRDE_SYNTAX;
        memcpy(name, input + position, name_length);
        name[name_length] = '\0';
        after = end;
    } else if (input[position] == '?' || input[position] == '$' ||
               input[position] == '#' || input[position] == '!' ||
               input[position] == '@' || input[position] == '*' ||
               (input[position] >= '0' && input[position] <= '9')) {
        name[0] = input[position];
        name[1] = '\0';
        name_length = 1U;
        after = position + 1U;
    } else {
        if (!akaos_wordexp_word_append(word, '$', quoted))
            return WRDE_NOSPACE;
        *cursor = position - 1U;
        return 0;
    }

    if (length_operator) {
        const char *value = getenv(name);
        size_t value_length = value ? strlen(value) : 0U;
        char number[32];
        int number_length = snprintf(number, sizeof(number), "%zu",
                                     value_length);
        if (number_length < 0 || (size_t)number_length >= sizeof(number))
            return WRDE_SYNTAX;
        if (!akaos_wordexp_append_expansion(word, words, number, ifs, quoted))
            return WRDE_NOSPACE;
        *cursor = after - 1U;
        return 0;
    }

    const char *value = NULL;
    char special_value[32];
    if (name_length == 1U && name[0] == '?') {
        value = "0";
    } else if (name_length == 1U && name[0] == '$') {
        int length = snprintf(special_value, sizeof(special_value), "%ld",
                              (long)getpid());
        if (length < 0 || (size_t)length >= sizeof(special_value))
            return WRDE_SYNTAX;
        value = special_value;
    } else if (name_length == 1U &&
               (name[0] == '#' || name[0] == '!' || name[0] == '@' ||
                name[0] == '*' || (name[0] >= '0' && name[0] <= '9'))) {
        value = "";
    } else {
        value = getenv(name);
    }

    int is_set = value != NULL;
    int is_nonempty = is_set && *value != '\0';
    if (operator_character == '-') {
        if (!is_set || (operator_colon && !is_nonempty)) {
            if (operator_colon || !is_set) {
                char *copy = (char *)malloc(fallback_length + 1U);
                if (!copy) return WRDE_NOSPACE;
                memcpy(copy, fallback, fallback_length);
                copy[fallback_length] = '\0';
                int appended = akaos_wordexp_append_expansion(
                    word, words, copy, ifs, quoted);
                free(copy);
                if (!appended) return WRDE_NOSPACE;
            }
            *cursor = after - 1U;
            return 0;
        }
    } else if (operator_character == '+') {
        if (!is_set || (operator_colon && !is_nonempty)) value = "";
        else {
            char *copy = (char *)malloc(fallback_length + 1U);
            if (!copy) return WRDE_NOSPACE;
            memcpy(copy, fallback, fallback_length);
            copy[fallback_length] = '\0';
            int appended = akaos_wordexp_append_expansion(
                word, words, copy, ifs, quoted);
            free(copy);
            if (!appended) return WRDE_NOSPACE;
            *cursor = after - 1U;
            return 0;
        }
    } else if (operator_character == '?') {
        if (!is_set || (operator_colon && !is_nonempty)) return WRDE_BADVAL;
    } else if (operator_character == '=') {
        if (!is_set || (operator_colon && !is_nonempty)) {
            char *copy = (char *)malloc(fallback_length + 1U);
            if (!copy) return WRDE_NOSPACE;
            memcpy(copy, fallback, fallback_length);
            copy[fallback_length] = '\0';
            if (setenv(name, copy, 1) != 0) {
                free(copy);
                return WRDE_NOSPACE;
            }
            value = getenv(name);
            free(copy);
            is_set = value != NULL;
        }
    }

    if (!is_set && (flags & WRDE_UNDEF) != 0) return WRDE_BADVAL;
    if (!akaos_wordexp_append_expansion(word, words, value, ifs, quoted))
        return WRDE_NOSPACE;
    *cursor = after - 1U;
    return 0;
}

static int akaos_wordexp_append_tilde(const char *input, size_t *cursor,
                                      struct akaos_wordexp_word *word) {
    size_t end = *cursor + 1U;
    while (input[end] && input[end] != '/') ++end;
    const char *home = NULL;
    char username[PATH_MAX];
    size_t username_length = end - *cursor - 1U;
    if (username_length == 0U) {
        home = getenv("HOME");
        if (!home) {
            struct passwd *account = getpwuid(getuid());
            if (account) home = account->pw_dir;
        }
    } else if (username_length < sizeof(username)) {
        memcpy(username, input + *cursor + 1U, username_length);
        username[username_length] = '\0';
        struct passwd *account = getpwnam(username);
        if (account) home = account->pw_dir;
    }
    if (!home) {
        for (size_t index = *cursor; index < end; ++index) {
            if (!akaos_wordexp_word_append(word, input[index], 0))
                return WRDE_NOSPACE;
        }
        *cursor = end - 1U;
        return 0;
    }
    if (!akaos_wordexp_word_append_text(word, home, 1)) return WRDE_NOSPACE;
    *cursor = end - 1U;
    return 0;
}

static int akaos_wordexp_parse(const char *input, int flags,
                               struct akaos_wordexp_words *words) {
    struct akaos_wordexp_word word;
    memset(&word, 0, sizeof(word));
    const char *ifs = getenv("IFS");
    if (!ifs) ifs = " \t\n";
    int quote = 0;
    int result = 0;

    for (size_t index = 0; input[index]; ++index) {
        char character = input[index];
        if (quote == '\'') {
            if (character == '\'') {
                quote = 0;
            } else if (!akaos_wordexp_word_append(&word, character, 1)) {
                result = WRDE_NOSPACE;
                break;
            }
            continue;
        }

        if (quote == '"') {
            if (character == '"') {
                quote = 0;
                continue;
            }
            if (character == '`') {
                result = WRDE_CMDSUB;
                break;
            }
            if (character == '\\') {
                char next = input[index + 1U];
                if (!next) {
                    result = WRDE_SYNTAX;
                    break;
                }
                if (next == '$' || next == '`' || next == '"' ||
                    next == '\\') {
                    if (!akaos_wordexp_word_append(&word, next, 1)) {
                        result = WRDE_NOSPACE;
                        break;
                    }
                    ++index;
                    continue;
                }
                if (!akaos_wordexp_word_append(&word, '\\', 1)) {
                    result = WRDE_NOSPACE;
                    break;
                }
                continue;
            }
            if (character == '$') {
                result = akaos_wordexp_append_parameter(
                    input, &index, &word, words, ifs, flags, 1);
                if (result != 0) break;
                continue;
            }
            if (!akaos_wordexp_word_append(&word, character, 1)) {
                result = WRDE_NOSPACE;
                break;
            }
            continue;
        }

        if (character == '\'' || character == '"') {
            quote = character;
            word.started = 1;
            continue;
        }
        if (character == '\\') {
            char next = input[index + 1U];
            if (!next) {
                result = WRDE_SYNTAX;
                break;
            }
            if (next == '\n') {
                ++index;
                continue;
            }
            if (!akaos_wordexp_word_append(&word, next, 1)) {
                result = WRDE_NOSPACE;
                break;
            }
            ++index;
            continue;
        }
        if (character == '`') {
            result = WRDE_CMDSUB;
            break;
        }
        if (character == '$') {
            result = akaos_wordexp_append_parameter(
                input, &index, &word, words, ifs, flags, 0);
            if (result != 0) break;
            continue;
        }
        if (character == '~' && !word.started) {
            result = akaos_wordexp_append_tilde(input, &index, &word);
            if (result != 0) break;
            continue;
        }
        if (character == '\n') {
            result = WRDE_BADCHAR;
            break;
        }
        if (character == ' ' || character == '\t') {
            if (!akaos_wordexp_word_finish(&word, words)) {
                result = WRDE_NOSPACE;
                break;
            }
            continue;
        }
        if (character == '|' || character == '&' || character == ';' ||
            character == '<' || character == '>' || character == '(' ||
            character == ')' || character == '{' || character == '}') {
            result = WRDE_BADCHAR;
            break;
        }
        if (!akaos_wordexp_word_append(&word, character, 0)) {
            result = WRDE_NOSPACE;
            break;
        }
    }

    if (result == 0 && quote != 0) result = WRDE_SYNTAX;
    if (result == 0 && !akaos_wordexp_word_finish(&word, words))
        result = WRDE_NOSPACE;
    akaos_wordexp_word_destroy(&word);
    return result;
}

static int akaos_wordexp_paths_reserve(struct akaos_wordexp_paths *paths,
                                       size_t additional) {
    if (additional > SIZE_MAX - paths->count) return 0;
    size_t needed = paths->count + additional;
    if (needed <= paths->capacity) return 1;
    size_t capacity = paths->capacity ? paths->capacity : 8U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) return 0;
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*paths->items)) return 0;
    char **items = (char **)realloc(paths->items,
                                    capacity * sizeof(*paths->items));
    if (!items) return 0;
    paths->items = items;
    paths->capacity = capacity;
    return 1;
}

static int akaos_wordexp_paths_push(struct akaos_wordexp_paths *paths,
                                    const char *path) {
    if (!akaos_wordexp_paths_reserve(paths, 1U)) return 0;
    size_t length = strlen(path);
    char *copy = (char *)malloc(length + 1U);
    if (!copy) return 0;
    memcpy(copy, path, length + 1U);
    paths->items[paths->count++] = copy;
    return 1;
}

static void akaos_wordexp_paths_destroy(struct akaos_wordexp_paths *paths) {
    for (size_t index = 0; index < paths->count; ++index)
        free(paths->items[index]);
    free(paths->items);
    memset(paths, 0, sizeof(*paths));
}

static int akaos_wordexp_path_join(const char *prefix, const char *component,
                                   char *output, size_t output_size) {
    size_t prefix_length = strlen(prefix);
    size_t component_length = strlen(component);
    int add_slash = prefix_length != 0U &&
                    prefix[prefix_length - 1U] != '/';
    size_t needed = prefix_length + (size_t)add_slash + component_length + 1U;
    if (needed > output_size) return 0;
    memcpy(output, prefix, prefix_length);
    size_t offset = prefix_length;
    if (add_slash) output[offset++] = '/';
    memcpy(output + offset, component, component_length);
    output[offset + component_length] = '\0';
    return 1;
}

static int akaos_wordexp_has_pattern(const struct akaos_wordexp_word *word,
                                     size_t start, size_t end) {
    for (size_t index = start; index < end; ++index) {
        if (!word->quoted[index] &&
            (word->text[index] == '*' || word->text[index] == '?' ||
             word->text[index] == '[')) return 1;
    }
    return 0;
}

static int akaos_wordexp_make_pattern(const struct akaos_wordexp_word *word,
                                      size_t start, size_t end,
                                      char *pattern, size_t pattern_size) {
    size_t output = 0U;
    for (size_t index = start; index < end; ++index) {
        char character = word->text[index];
        if (word->quoted[index] &&
            (character == '*' || character == '?' || character == '[' ||
             character == ']' || character == '\\')) {
            if (output + 1U >= pattern_size) return 0;
            pattern[output++] = '\\';
        }
        if (output + 1U >= pattern_size) return 0;
        pattern[output++] = character;
    }
    pattern[output] = '\0';
    return 1;
}

static int akaos_wordexp_expand_glob(const struct akaos_wordexp_word *word,
                                     struct akaos_wordexp_words *matches) {
    if (word->length == 0U || word->length >= PATH_MAX) return 0;
    int has_pattern = 0;
    for (size_t index = 0; index < word->length; ++index) {
        if (!word->quoted[index] &&
            (word->text[index] == '*' || word->text[index] == '?' ||
             word->text[index] == '[')) {
            has_pattern = 1;
            break;
        }
    }
    if (!has_pattern) return 0;

    struct akaos_wordexp_paths current;
    memset(&current, 0, sizeof(current));
    int absolute = word->text[0] == '/';
    if (!akaos_wordexp_paths_push(&current, absolute ? "/" : "")) {
        akaos_wordexp_paths_destroy(&current);
        return -1;
    }

    size_t cursor = absolute ? 1U : 0U;
    while (cursor < word->length) {
        while (cursor < word->length && word->text[cursor] == '/') ++cursor;
        if (cursor == word->length) break;
        size_t start = cursor;
        while (cursor < word->length && word->text[cursor] != '/') ++cursor;
        size_t end = cursor;
        size_t component_length = end - start;
        if (component_length == 0U) continue;
        char component[PATH_MAX];
        if (component_length >= sizeof(component)) {
            akaos_wordexp_paths_destroy(&current);
            return 0;
        }
        memcpy(component, word->text + start, component_length);
        component[component_length] = '\0';

        struct akaos_wordexp_paths next;
        memset(&next, 0, sizeof(next));
        if (akaos_wordexp_has_pattern(word, start, end)) {
            char pattern[PATH_MAX * 2U];
            if (!akaos_wordexp_make_pattern(word, start, end, pattern,
                                            sizeof(pattern))) {
                akaos_wordexp_paths_destroy(&current);
                return 0;
            }
            int allow_hidden = component[0] == '.';
            for (size_t path_index = 0; path_index < current.count;
                 ++path_index) {
                const char *base = current.items[path_index];
                DIR *directory = opendir(*base ? base : ".");
                if (!directory) continue;
                struct dirent *entry;
                while ((entry = readdir(directory)) != NULL) {
                    const char *name = entry->d_name;
                    if (name[0] == '.' && !allow_hidden) continue;
                    if (fnmatch(pattern, name, 0) != 0) continue;
                    char joined[PATH_MAX];
                    if (!akaos_wordexp_path_join(base, name, joined,
                                                 sizeof(joined))) continue;
                    if (!akaos_wordexp_paths_push(&next, joined)) {
                        closedir(directory);
                        akaos_wordexp_paths_destroy(&current);
                        akaos_wordexp_paths_destroy(&next);
                        return -1;
                    }
                }
                (void)closedir(directory);
            }
        } else {
            for (size_t path_index = 0; path_index < current.count;
                 ++path_index) {
                char joined[PATH_MAX];
                if (!akaos_wordexp_path_join(current.items[path_index],
                                             component, joined,
                                             sizeof(joined))) continue;
                if (!akaos_wordexp_paths_push(&next, joined)) {
                    akaos_wordexp_paths_destroy(&current);
                    akaos_wordexp_paths_destroy(&next);
                    return -1;
                }
            }
        }
        akaos_wordexp_paths_destroy(&current);
        current = next;
        if (current.count == 0U) return 0;
    }

    size_t old_count = matches->count;
    for (size_t index = 0; index < current.count; ++index) {
        if (!akaos_wordexp_words_push_copy(matches, current.items[index])) {
            akaos_wordexp_paths_destroy(&current);
            while (matches->count > old_count) {
                --matches->count;
                free(matches->items[matches->count]);
                free(matches->quoted[matches->count]);
            }
            return -1;
        }
    }
    akaos_wordexp_paths_destroy(&current);
    for (size_t left = old_count; left < matches->count; ++left) {
        for (size_t right = left + 1U; right < matches->count; ++right) {
            if (strcmp(matches->items[left], matches->items[right]) > 0) {
                char *temporary = matches->items[left];
                matches->items[left] = matches->items[right];
                matches->items[right] = temporary;
            }
        }
    }
    return (int)(matches->count - old_count);
}

void wordfree(wordexp_t *word_result) {
    if (!word_result) return;
    if (word_result->we_wordv) {
        for (size_t index = 0; index < word_result->we_wordc; ++index)
            free(word_result->we_wordv[word_result->we_offs + index]);
        free(word_result->we_wordv);
    }
    word_result->we_wordc = 0U;
    word_result->we_wordv = NULL;
    word_result->we_offs = 0U;
}

int wordexp(const char *input, wordexp_t *word_result, int flags) {
    const int valid_flags = WRDE_DOOFFS | WRDE_APPEND | WRDE_NOCMD |
                           WRDE_REUSE | WRDE_SHOWERR | WRDE_UNDEF;
    if (!input || !word_result || (flags & ~valid_flags) != 0 ||
        ((flags & WRDE_APPEND) != 0 && (flags & WRDE_REUSE) != 0)) {
        return WRDE_BADCHAR;
    }

    size_t offsets = 0U;
    if ((flags & (WRDE_DOOFFS | WRDE_REUSE)) != 0 &&
        (flags & WRDE_DOOFFS) != 0) {
        offsets = word_result->we_offs;
    }
    if ((flags & WRDE_REUSE) != 0) {
        wordfree(word_result);
        flags &= ~WRDE_REUSE;
    }
    if ((flags & WRDE_APPEND) != 0) {
        offsets = word_result->we_offs;
    } else if ((flags & WRDE_DOOFFS) != 0) {
        word_result->we_wordc = 0U;
        word_result->we_wordv = NULL;
        word_result->we_offs = offsets;
    } else {
        word_result->we_wordc = 0U;
        word_result->we_wordv = NULL;
        word_result->we_offs = 0U;
    }

    struct akaos_wordexp_words words;
    memset(&words, 0, sizeof(words));
    int status = akaos_wordexp_parse(input, flags, &words);
    if (status == 0) {
        struct akaos_wordexp_words expanded;
        memset(&expanded, 0, sizeof(expanded));
        for (size_t index = 0; index < words.count; ++index) {
            struct akaos_wordexp_word pattern;
            memset(&pattern, 0, sizeof(pattern));
            pattern.text = words.items[index];
            pattern.length = strlen(pattern.text);
            pattern.capacity = pattern.length + 1U;
            pattern.quoted = words.quoted[index];
            int matches = akaos_wordexp_expand_glob(&pattern, &expanded);
            if (matches < 0) {
                status = WRDE_NOSPACE;
                break;
            }
            if (matches == 0 &&
                !akaos_wordexp_words_push_owned(&expanded,
                                                words.items[index],
                                                words.quoted[index])) {
                status = WRDE_NOSPACE;
                break;
            }
            if (matches == 0) {
                words.items[index] = NULL;
                words.quoted[index] = NULL;
            } else {
                free(words.items[index]);
                free(words.quoted[index]);
                words.items[index] = NULL;
                words.quoted[index] = NULL;
            }
        }
        for (size_t index = 0; index < words.count; ++index) {
            free(words.items[index]);
            free(words.quoted[index]);
        }
        free(words.items);
        free(words.quoted);
        words = expanded;
    }

    if (status != 0) {
        akaos_wordexp_words_destroy(&words);
        if ((flags & WRDE_APPEND) == 0) {
            word_result->we_wordc = 0U;
            word_result->we_wordv = NULL;
            word_result->we_offs = offsets;
        }
        return status;
    }

    size_t previous_count = (flags & WRDE_APPEND) != 0
                                ? word_result->we_wordc : 0U;
    if (offsets > SIZE_MAX - previous_count ||
        offsets + previous_count > SIZE_MAX - words.count - 1U ||
        offsets + previous_count + words.count + 1U >
            SIZE_MAX / sizeof(char *)) {
        akaos_wordexp_words_destroy(&words);
        return WRDE_NOSPACE;
    }
    size_t vector_count = offsets + previous_count + words.count + 1U;
    char **vector = (char **)calloc(vector_count, sizeof(*vector));
    if (!vector) {
        akaos_wordexp_words_destroy(&words);
        return WRDE_NOSPACE;
    }
    if (previous_count != 0U) {
        if (!word_result->we_wordv) {
            free(vector);
            akaos_wordexp_words_destroy(&words);
            return WRDE_SYNTAX;
        }
        for (size_t index = 0; index < previous_count; ++index) {
            vector[offsets + index] =
                word_result->we_wordv[offsets + index];
        }
    }
    for (size_t index = 0; index < words.count; ++index) {
        vector[offsets + previous_count + index] = words.items[index];
        words.items[index] = NULL;
        free(words.quoted[index]);
        words.quoted[index] = NULL;
    }
    if ((flags & WRDE_APPEND) != 0) free(word_result->we_wordv);
    else if ((flags & WRDE_DOOFFS) == 0) word_result->we_offs = 0U;
    word_result->we_wordv = vector;
    word_result->we_wordc = previous_count + words.count;
    word_result->we_offs = offsets;
    free(words.items);
    free(words.quoted);
    return 0;
}

#include <netinet/in.h>
#include <sys/wait.h>
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

int system(const char *command) {
    if (!command) return 1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = {"/bin/akash", "-c", (char *)command, NULL};
        execv(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    off_t old = lseek(fd, 0, SEEK_CUR);
    if (old < 0) return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, count);
    int saved_errno = errno;
    (void)lseek(fd, old, SEEK_SET);
    errno = saved_errno;
    return n;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    off_t old = lseek(fd, 0, SEEK_CUR);
    if (old < 0) return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = write(fd, buf, count);
    int saved_errno = errno;
    (void)lseek(fd, old, SEEK_SET);
    errno = saved_errno;
    return n;
}

ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {
    return pread(fd, buf, count, offset);
}

ssize_t pwrite64(int fd, const void *buf, size_t count, off_t offset) {
    return pwrite(fd, buf, count, offset);
}

off_t lseek64(int descriptor, off_t offset, int whence) {
    return lseek(descriptor, offset, whence);
}

int truncate64(const char *path, off_t length) {
    return truncate(path, length);
}

int ftruncate64(int descriptor, off_t length) {
    return ftruncate(descriptor, length);
}

#include <sys/ipc.h>
#include <sys/shm.h>

key_t ftok(const char *pathname, int proj_id) {
    (void)pathname;
    (void)proj_id;
    return 1;
}

int shmget(key_t key, size_t size, int shmflg) {
    (void)key;
    (void)size;
    (void)shmflg;
    errno = ENOSYS;
    return -1;
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    (void)shmid;
    (void)shmaddr;
    (void)shmflg;
    errno = ENOSYS;
    return (void *)-1;
}

int shmdt(const void *shmaddr) {
    (void)shmaddr;
    errno = ENOSYS;
    return -1;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    (void)shmid;
    (void)cmd;
    (void)buf;
    errno = ENOSYS;
    return -1;
}

void __assert_fail(const char *assertion, const char *file,
                   unsigned int line, const char *function) {
    if (function) {
        fprintf(stderr, "%s:%u: %s: Assertion '%s' failed.\n",
                file ? file : "<unknown>", line, function,
                assertion ? assertion : "<assertion>");
    } else {
        fprintf(stderr, "%s:%u: Assertion '%s' failed.\n",
                file ? file : "<unknown>", line,
                assertion ? assertion : "<assertion>");
    }
    abort();
}
void __chk_fail(void) {
    write(STDERR_FILENO, "*** buffer overflow detected ***: terminated\n", 45);
    abort();
}

void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen) {
    if (len > destlen) __chk_fail();
    return memcpy(dest, src, len);
}

void *__memmove_chk(void *dest, const void *src, size_t len, size_t destlen) {
    if (len > destlen) __chk_fail();
    return memmove(dest, src, len);
}

void *__memset_chk(void *dest, int c, size_t len, size_t destlen) {
    if (len > destlen) __chk_fail();
    return memset(dest, c, len);
}

char *__strcpy_chk(char *dest, const char *src, size_t destlen) {
    size_t len = strlen(src);
    if (len >= destlen) __chk_fail();
    return (char *)memcpy(dest, src, len + 1);
}

char *__strncpy_chk(char *dest, const char *src, size_t len, size_t destlen) {
    if (len > destlen) __chk_fail();
    return strncpy(dest, src, len);
}

char *__strcat_chk(char *dest, const char *src, size_t destlen) {
    size_t dlen = strlen(dest);
    size_t slen = strlen(src);
    if (dlen + slen >= destlen) __chk_fail();
    memcpy(dest + dlen, src, slen + 1);
    return dest;
}

char *__strncat_chk(char *dest, const char *src, size_t len, size_t destlen) {
    size_t dlen = strlen(dest);
    size_t slen = strnlen(src, len);
    if (dlen + slen >= destlen) __chk_fail();
    memcpy(dest + dlen, src, slen);
    dest[dlen + slen] = '\0';
    return dest;
}

int __snprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *format, ...) {
    (void)flag;
    if (maxlen > slen) __chk_fail();
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(s, maxlen, format, ap);
    va_end(ap);
    return ret;
}

int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *format, va_list ap) {
    (void)flag;
    if (maxlen > slen) __chk_fail();
    return vsnprintf(s, maxlen, format, ap);
}

int __fprintf_chk(FILE *stream, int flag, const char *format, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int __printf_chk(int flag, const char *format, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}

int __vfprintf_chk(FILE *stream, int flag, const char *format, va_list ap) {
    (void)flag;
    return vfprintf(stream, format, ap);
}

int __vprintf_chk(int flag, const char *format, va_list ap) {
    (void)flag;
    return vprintf(format, ap);
}

long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return strtoul(nptr, endptr, base);
}

long long __isoc23_strtoll(const char *nptr, char **endptr, int base) {
    return strtoll(nptr, endptr, base);
}

unsigned long long __isoc23_strtoull(const char *nptr, char **endptr, int base) {
    return strtoull(nptr, endptr, base);
}

float __isoc23_strtof(const char *nptr, char **endptr) {
    return strtof(nptr, endptr);
}

double __isoc23_strtod(const char *nptr, char **endptr) {
    return strtod(nptr, endptr);
}

int mincore(void *addr, size_t length, unsigned char *vec) {
    if (!addr || !vec) {
        errno = EFAULT;
        return -1;
    }
    size_t page_count = (length + 4095) / 4096;
    for (size_t i = 0; i < page_count; ++i) {
        vec[i] = 1;
    }
    return 0;
}

size_t __fread_chk(void *ptr, size_t ptrlen, size_t size, size_t n, FILE *stream) {
    if (size > 0 && n > ptrlen / size) __chk_fail();
    return fread(ptr, size, n, stream);
}

int getpagesize(void) {
    return 4096;
}

int fstat64(int fd, struct stat *statbuf) {
    return fstat(fd, statbuf);
}

int stat64(const char *path, struct stat *statbuf) {
    return stat(path, statbuf);
}

int lstat64(const char *path, struct stat *statbuf) {
    return lstat(path, statbuf);
}

int fstatat64(int directory, const char *path, struct stat *statbuf, int flags) {
    return fstatat(directory, path, statbuf, flags);
}

static int32_t tolower_table[384];
static const int32_t *const tolower_table_ptr = &tolower_table[128];

const int32_t **__ctype_tolower_loc(void) {
    static int initialized = 0;
    if (!initialized) {
        for (int i = -128; i < 256; ++i) {
            tolower_table[128 + i] = (i >= 'A' && i <= 'Z') ? (i + ('a' - 'A')) : i;
        }
        initialized = 1;
    }
    return (const int32_t **)&tolower_table_ptr;
}

static int32_t toupper_table[384];
static const int32_t *const toupper_table_ptr = &toupper_table[128];

const int32_t **__ctype_toupper_loc(void) {
    static int initialized = 0;
    if (!initialized) {
        for (int i = -128; i < 256; ++i) {
            toupper_table[128 + i] = (i >= 'a' && i <= 'z') ? (i - ('a' - 'A')) : i;
        }
        initialized = 1;
    }
    return (const int32_t **)&toupper_table_ptr;
}

static uint16_t ctype_b_table[384];
static const uint16_t *const ctype_b_table_ptr = &ctype_b_table[128];

const uint16_t **__ctype_b_loc(void) {
    static int initialized = 0;
    if (!initialized) {
        for (int i = 0; i < 256; ++i) {
            uint16_t mask = 0;
            if (i >= '0' && i <= '9') mask |= 0x0800;
            if (i >= 'a' && i <= 'z') mask |= 0x0200;
            if (i >= 'A' && i <= 'Z') mask |= 0x0100;
            if (mask & 0x0300) mask |= 0x0400;
            if (mask & 0x0c00) mask |= 0x0008;
            if (i == ' ' || (i >= 9 && i <= 13)) mask |= 0x2000;
            if (i >= 0x20 && i <= 0x7e) mask |= 0x4000;
            if ((i >= 0x21 && i <= 0x2f) || (i >= 0x3a && i <= 0x40) ||
                (i >= 0x5b && i <= 0x60) || (i >= 0x7b && i <= 0x7e)) mask |= 0x0004;
            if ((i >= 0 && i <= 0x1f) || i == 0x7f) mask |= 0x0002;
            if ((mask & 0x0800) || (i >= 'a' && i <= 'f') || (i >= 'A' && i <= 'F')) mask |= 0x1000;
            if (i == ' ' || i == '\t') mask |= 0x0001;
            ctype_b_table[128 + i] = mask;
        }
        initialized = 1;
    }
    return (const uint16_t **)&ctype_b_table_ptr;
}

char *__progname = NULL;
char *program_invocation_name = NULL;
char *program_invocation_short_name = NULL;

void vwarn(const char *fmt, va_list args) {
    int saved_errno = errno;
    if (__progname && *__progname) fprintf(stderr, "%s: ", __progname);
    if (fmt) {
        vfprintf(stderr, fmt, args);
        fprintf(stderr, ": %s\n", strerror(saved_errno));
    } else {
        fprintf(stderr, "%s\n", strerror(saved_errno));
    }
}

void vwarnx(const char *fmt, va_list args) {
    if (__progname && *__progname) fprintf(stderr, "%s: ", __progname);
    if (fmt) vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void verr(int eval, const char *fmt, va_list args) {
    vwarn(fmt, args);
    exit(eval);
}

void verrx(int eval, const char *fmt, va_list args) {
    vwarnx(fmt, args);
    exit(eval);
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void warnx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

void err(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verr(eval, fmt, ap);
    va_end(ap);
}

void errx(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verrx(eval, fmt, ap);
    va_end(ap);
}

size_t confstr(int name, char *buf, size_t len) {
    const char *val = "";
    if (name == _CS_PATH) val = "/bin:/usr/bin:/usr/local/bin";
    else if (name == _CS_GNU_LIBC_VERSION) val = "akaOS libc 0.2";
    else if (name == _CS_GNU_LIBPTHREAD_VERSION) val = "akaOS pthread 0.2";
    else {
        errno = EINVAL;
        return 0;
    }
    size_t val_len = strlen(val) + 1;
    if (len > 0 && buf) {
        strncpy(buf, val, len);
        buf[len - 1] = 0;
    }
    return val_len;
}

void globfree(glob_t *pglob) {
    if (!pglob) return;
    if (pglob->gl_pathv) {
        for (size_t i = 0; i < pglob->gl_pathc; ++i) {
            free(pglob->gl_pathv[pglob->gl_offs + i]);
        }
        free(pglob->gl_pathv);
        pglob->gl_pathv = NULL;
    }
    pglob->gl_pathc = 0;
}

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *epath, int eerrno), glob_t *pglob) {
    (void)errfunc;
    if (!pattern || !pglob) return GLOB_ABORTED;

    char resolved[1024];
    resolved[0] = 0;

    if ((flags & (GLOB_TILDE | GLOB_TILDE_CHECK)) && pattern[0] == '~') {
        const char *home = getenv("HOME");
        if (!home || !*home) home = "/root";
        if (pattern[1] == '/' || pattern[1] == 0) {
            snprintf(resolved, sizeof(resolved), "%s%s", home, pattern + 1);
            pattern = resolved;
        }
    }

    if (!(flags & GLOB_APPEND)) {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = NULL;
        if (!(flags & GLOB_DOOFFS)) pglob->gl_offs = 0;
    }

    if (!strpbrk(pattern, "*?[")) {
        struct stat st;
        if (stat(pattern, &st) == 0 || (flags & GLOB_NOCHECK)) {
            size_t total = pglob->gl_offs + pglob->gl_pathc + 2;
            char **new_pathv = (char **)realloc(pglob->gl_pathv, total * sizeof(char *));
            if (!new_pathv) return GLOB_NOSPACE;
            pglob->gl_pathv = new_pathv;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = strdup(pattern);
            pglob->gl_pathc++;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
            return 0;
        }
        return GLOB_NOMATCH;
    }

    char dirpath[1024];
    const char *last_slash = strrchr(pattern, '/');
    const char *pat_file = pattern;
    if (last_slash) {
        size_t dlen = (size_t)(last_slash - pattern);
        if (dlen >= sizeof(dirpath)) return GLOB_NOSPACE;
        memcpy(dirpath, pattern, dlen);
        dirpath[dlen] = 0;
        pat_file = last_slash + 1;
    } else {
        strcpy(dirpath, ".");
    }

    DIR *d = opendir(dirpath[0] ? dirpath : "/");
    if (!d) {
        if (flags & GLOB_NOCHECK) {
            size_t total = pglob->gl_offs + pglob->gl_pathc + 2;
            char **new_pathv = (char **)realloc(pglob->gl_pathv, total * sizeof(char *));
            if (!new_pathv) return GLOB_NOSPACE;
            pglob->gl_pathv = new_pathv;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = strdup(pattern);
            pglob->gl_pathc++;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
            return 0;
        }
        return GLOB_NOMATCH;
    }

    struct dirent *ent;
    size_t matched = 0;
    while ((ent = readdir(d)) != NULL) {
        if (fnmatch(pat_file, ent->d_name, FNM_PATHNAME) == 0) {
            char entry_path[1024];
            if (last_slash) {
                snprintf(entry_path, sizeof(entry_path), "%s/%s", dirpath, ent->d_name);
            } else {
                snprintf(entry_path, sizeof(entry_path), "%s", ent->d_name);
            }
            size_t total = pglob->gl_offs + pglob->gl_pathc + 2;
            char **new_pathv = (char **)realloc(pglob->gl_pathv, total * sizeof(char *));
            if (!new_pathv) {
                closedir(d);
                return GLOB_NOSPACE;
            }
            pglob->gl_pathv = new_pathv;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = strdup(entry_path);
            pglob->gl_pathc++;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
            matched++;
        }
    }
    closedir(d);

    if (matched == 0) {
        if (flags & GLOB_NOCHECK) {
            size_t total = pglob->gl_offs + pglob->gl_pathc + 2;
            char **new_pathv = (char **)realloc(pglob->gl_pathv, total * sizeof(char *));
            if (!new_pathv) return GLOB_NOSPACE;
            pglob->gl_pathv = new_pathv;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = strdup(pattern);
            pglob->gl_pathc++;
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
            return 0;
        }
        return GLOB_NOMATCH;
    }

    if (!(flags & GLOB_NOSORT) && pglob->gl_pathc > 1) {
        qsort(pglob->gl_pathv + pglob->gl_offs, pglob->gl_pathc,
              sizeof(char *), (int (*)(const void *, const void *))strcmp);
    }

    return 0;
}

enum __akaos_enc {
    ENC_UTF8,
    ENC_UCS2BE,
    ENC_ASCII
};

struct __akaos_iconv_ctx {
    enum __akaos_enc from;
    enum __akaos_enc to;
};

static enum __akaos_enc parse_enc(const char *name) {
    if (!name) return ENC_UTF8;
    if (strncasecmp(name, "UCS-2BE", 7) == 0 || strncasecmp(name, "UTF-16BE", 8) == 0)
        return ENC_UCS2BE;
    if (strncasecmp(name, "ASCII", 5) == 0 || strncasecmp(name, "US-ASCII", 8) == 0)
        return ENC_ASCII;
    return ENC_UTF8;
}

iconv_t iconv_open(const char *tocode, const char *fromcode) {
    if (!tocode || !fromcode) {
        errno = EINVAL;
        return (iconv_t)-1;
    }
    struct __akaos_iconv_ctx *ctx = (struct __akaos_iconv_ctx *)malloc(sizeof(struct __akaos_iconv_ctx));
    if (!ctx) {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    ctx->to = parse_enc(tocode);
    ctx->from = parse_enc(fromcode);
    return (iconv_t)ctx;
}

int iconv_close(iconv_t cd) {
    if (!cd || cd == (iconv_t)-1) {
        errno = EBADF;
        return -1;
    }
    free(cd);
    return 0;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft) {
    if (!cd || cd == (iconv_t)-1) {
        errno = EBADF;
        return (size_t)-1;
    }
    if (!inbuf || !*inbuf) return 0;
    if (!outbuf || !*outbuf || !inbytesleft || !outbytesleft) return 0;

    struct __akaos_iconv_ctx *ctx = (struct __akaos_iconv_ctx *)cd;
    size_t in_left = *inbytesleft;
    size_t out_left = *outbytesleft;
    const unsigned char *in = (const unsigned char *)*inbuf;
    unsigned char *out = (unsigned char *)*outbuf;
    size_t non_identical = 0;

    if (ctx->from == ENC_UCS2BE && ctx->to == ENC_UTF8) {
        while (in_left >= 2 && out_left > 0) {
            uint32_t cp = ((uint32_t)in[0] << 8) | (uint32_t)in[1];
            in += 2;
            in_left -= 2;

            if (cp < 0x80) {
                *out++ = (unsigned char)cp;
                out_left--;
            } else if (cp < 0x800) {
                if (out_left < 2) { errno = E2BIG; goto done; }
                *out++ = (unsigned char)(0xc0 | (cp >> 6));
                *out++ = (unsigned char)(0x80 | (cp & 0x3f));
                out_left -= 2;
            } else {
                if (out_left < 3) { errno = E2BIG; goto done; }
                *out++ = (unsigned char)(0xe0 | (cp >> 12));
                *out++ = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
                *out++ = (unsigned char)(0x80 | (cp & 0x3f));
                out_left -= 3;
            }
            non_identical++;
        }
    } else if (ctx->from == ENC_UTF8 && ctx->to == ENC_UCS2BE) {
        while (in_left > 0 && out_left >= 2) {
            uint32_t cp = 0;
            size_t step = 0;
            if (in[0] < 0x80) {
                cp = in[0];
                step = 1;
            } else if ((in[0] & 0xe0) == 0xc0 && in_left >= 2) {
                cp = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
                step = 2;
            } else if ((in[0] & 0xf0) == 0xe0 && in_left >= 3) {
                cp = ((in[0] & 0x0f) << 12) | ((in[1] & 0x3f) << 6) | (in[2] & 0x3f);
                step = 3;
            } else {
                break;
            }
            in += step;
            in_left -= step;
            *out++ = (unsigned char)(cp >> 8);
            *out++ = (unsigned char)(cp & 0xff);
            out_left -= 2;
            non_identical++;
        }
    } else {
        size_t n = in_left < out_left ? in_left : out_left;
        memcpy(out, in, n);
        in += n;
        in_left -= n;
        out += n;
        out_left -= n;
    }

done:
    *inbuf = (char *)in;
    *inbytesleft = in_left;
    *outbuf = (char *)out;
    *outbytesleft = out_left;
    return non_identical;
}

int posix_fallocate(int fd, off_t offset, off_t len) {
    if (offset < 0 || len <= 0) return EINVAL;
    return ftruncate(fd, offset + len) == 0 ? 0 : errno;
}

