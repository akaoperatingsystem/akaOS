#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef AKAOS_HOST_TEST
#include <dirent.h>
#include <sys/stat.h>
#define NAME_MAX 255
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define INITIAL_ENTRIES 32
#define NAME_BUF (NAME_MAX + 1)
#define ESCAPE_BUF ((NAME_MAX + 1) * 4)

static char (*entries)[NAME_BUF];
static size_t count;
static size_t capacity;

static int name_cmp(const void *a, const void *b) {
    const char *left = (const char *)a;
    const char *right = (const char *)b;
    return strcmp(left, right);
}

static size_t escape_name(const char *name, char *escaped, size_t capacity) {
    size_t out = 0;
    if (capacity == 0) return 0;
    for (size_t i = 0; name[i] && out + 4 < capacity; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 0x21 && c <= 0x7e && c != '\\') {
            escaped[out++] = (char)c;
        } else if (c == ' ') {
            escaped[out++] = '\\'; escaped[out++] = ' ';
        } else if (c == '\t') {
            escaped[out++] = '\\'; escaped[out++] = 't';
        } else if (c == '\n') {
            escaped[out++] = '\\'; escaped[out++] = 'n';
        } else if (c == '\r') {
            escaped[out++] = '\\'; escaped[out++] = 'r';
        } else if (c == '\\') {
            escaped[out++] = '\\'; escaped[out++] = '\\';
        } else {
            escaped[out++] = '\\';
            escaped[out++] = (char)('0' + ((c >> 6) & 7));
            escaped[out++] = (char)('0' + ((c >> 3) & 7));
            escaped[out++] = (char)('0' + (c & 7));
        }
    }
    escaped[out] = '\0';
    return out;
}

static size_t print_escaped(const char *name) {
    char escaped[ESCAPE_BUF];
    size_t length = escape_name(name, escaped, sizeof(escaped));
    fputs(escaped, stdout);
    return length;
}

static int list_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "dir: cannot access '%s': %s\n", path, strerror(errno));
        return 1;
    }

    count = 0;
    capacity = INITIAL_ENTRIES;
    entries = malloc(capacity * sizeof(*entries));
    if (!entries) { closedir(dir); fprintf(stderr, "dir: out of memory\n"); return 1; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Match ls -C's default: omit dotfiles and the . / .. entries. */
        if (entry->d_name[0] == '.') continue;
        if (count == capacity) {
            if (capacity > SIZE_MAX / (2U * sizeof(*entries))) {
                free(entries); entries = NULL; closedir(dir);
                fprintf(stderr, "dir: too many directory entries\n"); return 1;
            }
            size_t new_capacity = capacity * 2U;
            char (*grown)[NAME_BUF] = realloc(entries, new_capacity * sizeof(*entries));
            if (!grown) { free(entries); entries = NULL; closedir(dir); fprintf(stderr, "dir: out of memory\n"); return 1; }
            entries = grown; capacity = new_capacity;
        }
        size_t name_length = strlen(entry->d_name);
        if (name_length >= NAME_BUF) name_length = NAME_BUF - 1;
        memcpy(entries[count], entry->d_name, name_length);
        entries[count][name_length] = '\0';
        count++;
    }
    closedir(dir);

    qsort(entries, count, sizeof(entries[0]), name_cmp);

    size_t *widths = malloc(count * sizeof(*widths));
    if (count && !widths) { free(entries); entries = NULL; return 1; }
    size_t max_width = 0;
    for (size_t i = 0; i < count; ++i) {
        char escaped[ESCAPE_BUF];
        widths[i] = escape_name(entries[i], escaped, sizeof(escaped));
        if (widths[i] > max_width) max_width = widths[i];
    }

    /* Choose as many columns as fit, then fill vertically like ls -C. */
    size_t cols = max_width < 80 ? 80 / (max_width + 2) : 1;
    if (cols == 0) cols = 1;
    size_t rows = count ? (count + cols - 1) / cols : 0;
    size_t *col_width = malloc(cols * sizeof(*col_width));
    if (cols && !col_width) { free(widths); free(entries); entries = NULL; return 1; }
    for (size_t col = 0; col < cols; ++col) col_width[col] = 0;
    for (size_t col = 0; col < cols; ++col) {
        for (size_t row = 0; row < rows; ++row) {
            size_t idx = col * rows + row;
            if (idx < count && widths[idx] > col_width[col]) col_width[col] = widths[idx];
        }
    }

    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            size_t idx = col * rows + row;
            if (idx >= count) continue;
            size_t written = print_escaped(entries[idx]);
            if (col + 1 < cols && (col + 1) * rows + row < count) {
                size_t pad_to = col_width[col] + 2;
                while (written < pad_to) { putchar(' '); ++written; }
            }
        }
        putchar('\n');
    }
    free(col_width);
    free(widths);
    free(entries);
    entries = NULL;
    return 0;
}

int main(int argc, char **argv) {
    int status = 0;
    int first = 1;
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: dir [DIRECTORY]...\nBriefly list directory contents. Equivalent to ls -C -b.");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("dir (akaOS coreutils) 1.0");
        return 0;
    }
    if (argc == 1) return list_directory(".");
    for (int i = first; i < argc; ++i) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            fprintf(stderr, "dir: cannot access '%s': %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            print_escaped(argv[i]);
            putchar('\n');
            continue;
        }
        if (argc > 2) {
            if (i > first) putchar('\n');
            printf("%s:\n", argv[i]);
        }
        if (list_directory(argv[i]) != 0) status = 1;
    }
    return status;
}
