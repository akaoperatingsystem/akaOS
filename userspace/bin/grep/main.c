#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GREP_VERSION "3.8 (akaOS coreutils)"

/* Configuration flags */
static int opt_ignore_case = 0;       /* -i, -y */
static int opt_invert_match = 0;      /* -v */
static int opt_line_number = 0;       /* -n */
static int opt_count_only = 0;        /* -c */
static int opt_quiet = 0;             /* -q */
static int opt_silent = 0;            /* -s */
static int opt_files_with_matches = 0;/* -l */
static int opt_files_without_match = 0;/* -L */
static int opt_byte_offset = 0;       /* -b */
static int opt_word_regexp = 0;       /* -w */
static int opt_line_regexp = 0;       /* -x */
static int opt_fixed_strings = 0;     /* -F */
static int opt_extended_regexp = 0;   /* -E */
static int opt_only_matching = 0;     /* -o */
static int opt_with_filename = -1;    /* -H (1), -h (0), default (-1) */
static int opt_recursive = 0;         /* -r, -R */
static int opt_null_data = 0;         /* -Z */
static long opt_max_count = -1;       /* -m NUM */
static int opt_before_context = 0;    /* -B NUM */
static int opt_after_context = 0;     /* -A NUM */
static int opt_color = 0;             /* --color */

/* Structure for multiple patterns */
struct pattern_list {
    char **patterns;
    int count;
    int capacity;
};

static void add_pattern(struct pattern_list *list, const char *pat) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        list->patterns = realloc(list->patterns, sizeof(char *) * new_cap);
        list->capacity = new_cap;
    }
    list->patterns[list->count++] = strdup(pat);
}

static void free_patterns(struct pattern_list *list) {
    for (int i = 0; i < list->count; ++i) {
        free(list->patterns[i]);
    }
    if (list->patterns) free(list->patterns);
}

static void print_version(void) {
    fprintf(stderr, "grep %s\n", GREP_VERSION);
}

static void print_usage(void) {
    printf("Usage: grep [OPTION]... PATTERNS [FILE]...\n"
           "Search for PATTERNS in each FILE.\n\n"
           "Pattern selection and interpretation:\n"
           "  -E, --extended-regexp     PATTERNS are extended regular expressions\n"
           "  -F, --fixed-strings       PATTERNS are strings\n"
           "  -e, --regexp=PATTERNS     use PATTERNS for matching\n"
           "  -f, --file=FILE           take PATTERNS from FILE\n"
           "  -i, --ignore-case         ignore case distinctions in patterns and data\n"
           "  -w, --word-regexp         match only whole words\n"
           "  -x, --line-regexp         match only whole lines\n\n"
           "Miscellaneous:\n"
           "  -s, --no-messages         suppress error messages\n"
           "  -v, --invert-match        select non-matching lines\n"
           "  -V, --version             display version information and exit\n"
           "      --help                display this help text and exit\n\n"
           "Output control:\n"
           "  -m, --max-count=NUM       stop after NUM selected lines\n"
           "  -b, --byte-offset         print the byte offset with output lines\n"
           "  -n, --line-number         print line number with output lines\n"
           "  -H, --with-filename       print file name with output lines\n"
           "  -h, --no-filename         suppress the file name prefix on output\n"
           "  -o, --only-matching       show only nonempty parts of lines matching\n"
           "  -q, --quiet, --silent     suppress all normal output\n"
           "  -c, --count               print only a count of selected lines per FILE\n"
           "  -l, --files-with-matches  print only names of FILEs with selected lines\n"
           "  -L, --files-without-match print only names of FILEs with no selected lines\n"
           "  -Z, --null                print 0 byte after FILE name\n"
           "  -r, -R, --recursive       like --directories=recurse\n");
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* Match fixed string helper */
static const char *match_fixed(const char *line, const char *pat, size_t *match_len) {
    size_t pat_len = strlen(pat);
    size_t line_len = strlen(line);
    *match_len = pat_len;

    if (opt_line_regexp) {
        if (pat_len != line_len) return NULL;
        for (size_t i = 0; i < pat_len; ++i) {
            char c1 = line[i];
            char c2 = pat[i];
            if (opt_ignore_case) {
                c1 = to_lower(c1);
                c2 = to_lower(c2);
            }
            if (c1 != c2) return NULL;
        }
        return line;
    }

    if (pat_len == 0) return line;
    if (pat_len > line_len) return NULL;

    for (size_t i = 0; i <= line_len - pat_len; ++i) {
        size_t j = 0;
        while (j < pat_len) {
            char c1 = line[i + j];
            char c2 = pat[j];
            if (opt_ignore_case) {
                c1 = to_lower(c1);
                c2 = to_lower(c2);
            }
            if (c1 != c2) break;
            ++j;
        }
        if (j == pat_len) {
            if (opt_word_regexp) {
                int left_boundary = (i == 0) || !is_word_char(line[i - 1]);
                int right_boundary = (i + pat_len == line_len) || !is_word_char(line[i + pat_len]);
                if (!left_boundary || !right_boundary) {
                    continue;
                }
            }
            return line + i;
        }
    }
    return NULL;
}

/* Match regex helper */
static int match_regex(const regex_t *preg, const char *line, size_t *out_start, size_t *out_len) {
    regmatch_t pm;
    int res = regexec(preg, line, 1, &pm, 0);
    if (res == 0) {
        if (opt_line_regexp) {
            if (pm.rm_so != 0 || (size_t)pm.rm_eo != strlen(line)) {
                return 0;
            }
        }
        if (opt_word_regexp) {
            size_t so = pm.rm_so;
            size_t eo = pm.rm_eo;
            size_t len = strlen(line);
            int left = (so == 0) || !is_word_char(line[so - 1]);
            int right = (eo == len) || !is_word_char(line[eo]);
            if (!left || !right) {
                return 0;
            }
        }
        *out_start = pm.rm_so;
        *out_len = pm.rm_eo - pm.rm_so;
        return 1;
    }
    return 0;
}

/* Line context buffer for -B and -A */
#define MAX_CONTEXT 32
struct context_entry {
    char *line;
    int line_num;
    off_t byte_offset;
};

static int grep_stream(int fd, const char *filename,
                       struct pattern_list *fixed_pats,
                       regex_t *regexes, int regex_count,
                       int show_filename) {
    char buffer[1024];
    char current_line[2048];
    size_t line_len = 0;
    int line_num = 1;
    off_t line_byte_offset = 0;
    off_t cur_byte_pos = 0;
    int matched_lines = 0;

    struct context_entry history[MAX_CONTEXT];
    int hist_count = 0;
    int remaining_after = 0;

    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (!opt_silent && !opt_quiet) {
                fprintf(stderr, "grep: %s: %s\n", filename, strerror(errno));
            }
            return -1;
        }
        if (n == 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            char c = buffer[i];
            cur_byte_pos++;

            if (c == '\n') {
                current_line[line_len] = '\0';

                /* Test matching across patterns */
                int line_matched = 0;
                size_t m_start = 0, m_len = 0;

                if (opt_fixed_strings) {
                    for (int p = 0; p < fixed_pats->count; ++p) {
                        const char *m = match_fixed(current_line, fixed_pats->patterns[p], &m_len);
                        if (m != NULL) {
                            line_matched = 1;
                            m_start = (size_t)(m - current_line);
                            break;
                        }
                    }
                } else {
                    for (int p = 0; p < regex_count; ++p) {
                        if (match_regex(&regexes[p], current_line, &m_start, &m_len)) {
                            line_matched = 1;
                            break;
                        }
                    }
                }

                if (opt_invert_match) {
                    line_matched = !line_matched;
                }

                if (line_matched) {
                    matched_lines++;

                    if (opt_quiet) {
                        return 1;
                    }

                    if (opt_files_with_matches) {
                        if (opt_null_data) printf("%s%c", filename, '\0');
                        else printf("%s\n", filename);
                        return 1;
                    }

                    if (!opt_count_only && !opt_files_without_match) {
                        /* Print before context */
                        if (opt_before_context > 0 && hist_count > 0) {
                            for (int h = 0; h < hist_count; ++h) {
                                if (show_filename) printf("%s-", filename);
                                if (opt_line_number) printf("%d-", history[h].line_num);
                                if (opt_byte_offset) printf("%ld-", (long)history[h].byte_offset);
                                printf("%s\n", history[h].line);
                                free(history[h].line);
                            }
                            hist_count = 0;
                        }

                        /* Print current matched line */
                        if (show_filename) {
                            if (opt_null_data) printf("%s%c", filename, '\0');
                            else printf("%s:", filename);
                        }
                        if (opt_line_number) printf("%d:", line_num);
                        if (opt_byte_offset) printf("%ld:", (long)line_byte_offset);

                        if (opt_only_matching && !opt_invert_match) {
                            char sub[512];
                            if (m_len < sizeof(sub)) {
                                strncpy(sub, current_line + m_start, m_len);
                                sub[m_len] = '\0';
                                printf("%s\n", sub);
                            }
                        } else {
                            printf("%s\n", current_line);
                        }

                        remaining_after = opt_after_context;
                    }

                    if (opt_max_count > 0 && matched_lines >= opt_max_count) {
                        break;
                    }
                } else {
                    /* Non-matching line: print if in trailing after-context */
                    if (remaining_after > 0 && !opt_count_only && !opt_files_without_match && !opt_quiet) {
                        if (show_filename) printf("%s-", filename);
                        if (opt_line_number) printf("%d-", line_num);
                        if (opt_byte_offset) printf("%ld-", (long)line_byte_offset);
                        printf("%s\n", current_line);
                        remaining_after--;
                    } else if (opt_before_context > 0 && !opt_count_only && !opt_files_without_match) {
                        /* Push into history queue */
                        if (hist_count >= opt_before_context) {
                            free(history[0].line);
                            for (int h = 1; h < hist_count; ++h) {
                                history[h - 1] = history[h];
                            }
                            hist_count--;
                        }
                        history[hist_count].line = strdup(current_line);
                        history[hist_count].line_num = line_num;
                        history[hist_count].byte_offset = line_byte_offset;
                        hist_count++;
                    }
                }

                line_len = 0;
                line_num++;
                line_byte_offset = cur_byte_pos;
            } else {
                if (line_len < sizeof(current_line) - 1) {
                    current_line[line_len++] = c;
                }
            }
        }
        if (opt_max_count > 0 && matched_lines >= opt_max_count) break;
    }

    /* Trailing line without newline */
    if (line_len > 0 && (opt_max_count < 0 || matched_lines < opt_max_count)) {
        current_line[line_len] = '\0';
        int line_matched = 0;
        size_t m_start = 0, m_len = 0;

        if (opt_fixed_strings) {
            for (int p = 0; p < fixed_pats->count; ++p) {
                const char *m = match_fixed(current_line, fixed_pats->patterns[p], &m_len);
                if (m != NULL) {
                    line_matched = 1;
                    m_start = (size_t)(m - current_line);
                    break;
                }
            }
        } else {
            for (int p = 0; p < regex_count; ++p) {
                if (match_regex(&regexes[p], current_line, &m_start, &m_len)) {
                    line_matched = 1;
                    break;
                }
            }
        }

        if (opt_invert_match) line_matched = !line_matched;

        if (line_matched) {
            matched_lines++;
            if (opt_quiet) return 1;
            if (opt_files_with_matches) {
                if (opt_null_data) printf("%s%c", filename, '\0');
                else printf("%s\n", filename);
                return 1;
            }
            if (!opt_count_only && !opt_files_without_match) {
                if (show_filename) {
                    if (opt_null_data) printf("%s%c", filename, '\0');
                    else printf("%s:", filename);
                }
                if (opt_line_number) printf("%d:", line_num);
                if (opt_byte_offset) printf("%ld:", (long)line_byte_offset);
                printf("%s\n", current_line);
            }
        }
    }

    /* Cleanup remaining context buffer */
    for (int h = 0; h < hist_count; ++h) {
        free(history[h].line);
    }

    if (opt_count_only && !opt_quiet) {
        if (show_filename) printf("%s:", filename);
        printf("%d\n", matched_lines);
    }

    if (opt_files_without_match && matched_lines == 0 && !opt_quiet) {
        if (opt_null_data) printf("%s%c", filename, '\0');
        else printf("%s\n", filename);
    }

    return matched_lines;
}

static int grep_target(const char *path,
                       struct pattern_list *fixed_pats,
                       regex_t *regexes, int regex_count,
                       int show_filename);

static int grep_directory_recursive(const char *dir_path,
                                    struct pattern_list *fixed_pats,
                                    regex_t *regexes, int regex_count) {
    DIR *d = opendir(dir_path);
    if (!d) {
        if (!opt_silent && !opt_quiet) {
            fprintf(stderr, "grep: %s: %s\n", dir_path, strerror(errno));
        }
        return 0;
    }

    int total_matched = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char subpath[512];
        if (strcmp(dir_path, "/") == 0) {
            snprintf(subpath, sizeof(subpath), "/%s", ent->d_name);
        } else {
            snprintf(subpath, sizeof(subpath), "%s/%s", dir_path, ent->d_name);
        }
        int m = grep_target(subpath, fixed_pats, regexes, regex_count, 1);
        if (m > 0) total_matched += m;
    }
    closedir(d);
    return total_matched;
}

static int grep_target(const char *path,
                       struct pattern_list *fixed_pats,
                       regex_t *regexes, int regex_count,
                       int show_filename) {
    if (strcmp(path, "-") == 0) {
        int m = grep_stream(STDIN_FILENO, "(standard input)", fixed_pats, regexes, regex_count, show_filename);
        return m > 0 ? m : 0;
    }

    struct stat st;
    if (lstat(path, &st) != 0) {
        if (!opt_silent && !opt_quiet) {
            fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        }
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (opt_recursive) {
            return grep_directory_recursive(path, fixed_pats, regexes, regex_count);
        } else {
            if (!opt_silent && !opt_quiet) {
                fprintf(stderr, "grep: %s: Is a directory\n", path);
            }
            return 0;
        }
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (!opt_silent && !opt_quiet) {
            fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        }
        return -1;
    }

    int m = grep_stream(fd, path, fixed_pats, regexes, regex_count, show_filename);
    close(fd);
    return m > 0 ? m : 0;
}

int main(int argc, char **argv) {
    struct pattern_list raw_patterns = {0};
    char *files[256];
    int file_count = 0;

    int i = 1;
    while (i < argc) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-e") == 0 || strcmp(arg, "--regexp") == 0) {
            if (i + 1 < argc) {
                add_pattern(&raw_patterns, argv[++i]);
            }
            i++;
            continue;
        }
        if (strncmp(arg, "-e", 2) == 0 && strlen(arg) > 2) {
            add_pattern(&raw_patterns, arg + 2);
            i++;
            continue;
        }
        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--file") == 0) {
            if (i + 1 < argc) {
                const char *fpath = argv[++i];
                FILE *pf = fopen(fpath, "r");
                if (pf) {
                    char pline[512];
                    while (fgets(pline, sizeof(pline), pf)) {
                        pline[strcspn(pline, "\r\n")] = '\0';
                        add_pattern(&raw_patterns, pline);
                    }
                    fclose(pf);
                }
            }
            i++;
            continue;
        }
        if (strcmp(arg, "-m") == 0 || strcmp(arg, "--max-count") == 0) {
            if (i + 1 < argc) opt_max_count = atol(argv[++i]);
            i++;
            continue;
        }
        if (strcmp(arg, "-A") == 0 || strcmp(arg, "--after-context") == 0) {
            if (i + 1 < argc) opt_after_context = atoi(argv[++i]);
            i++;
            continue;
        }
        if (strcmp(arg, "-B") == 0 || strcmp(arg, "--before-context") == 0) {
            if (i + 1 < argc) opt_before_context = atoi(argv[++i]);
            i++;
            continue;
        }
        if (strcmp(arg, "-C") == 0 || strcmp(arg, "--context") == 0) {
            if (i + 1 < argc) {
                int c = atoi(argv[++i]);
                opt_before_context = c;
                opt_after_context = c;
            }
            i++;
            continue;
        }
        if (strcmp(arg, "--color") == 0 || strcmp(arg, "--colour") == 0) {
            opt_color = 1;
            i++;
            continue;
        }
        if (strncmp(arg, "--color=", 8) == 0 || strncmp(arg, "--colour=", 9) == 0) {
            opt_color = 1;
            i++;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            i++;
            while (i < argc && file_count < 256) {
                files[file_count++] = argv[i++];
            }
            break;
        }

        if (arg[0] == '-' && arg[1] != '\0') {
            for (size_t c = 1; arg[c]; ++c) {
                switch (arg[c]) {
                    case 'i': case 'y': opt_ignore_case = 1; break;
                    case 'v': opt_invert_match = 1; break;
                    case 'n': opt_line_number = 1; break;
                    case 'c': opt_count_only = 1; break;
                    case 'q': opt_quiet = 1; break;
                    case 's': opt_silent = 1; break;
                    case 'l': opt_files_with_matches = 1; break;
                    case 'L': opt_files_without_match = 1; break;
                    case 'b': opt_byte_offset = 1; break;
                    case 'w': opt_word_regexp = 1; break;
                    case 'x': opt_line_regexp = 1; break;
                    case 'F': opt_fixed_strings = 1; break;
                    case 'E': opt_extended_regexp = 1; break;
                    case 'o': opt_only_matching = 1; break;
                    case 'H': opt_with_filename = 1; break;
                    case 'h': opt_with_filename = 0; break;
                    case 'r': case 'R': opt_recursive = 1; break;
                    case 'Z': opt_null_data = 1; break;
                    case 'a': /* Treat binary as text (pass through) */ break;
                    case 'I': /* Ignore binary files */ break;
                    default:
                        fprintf(stderr, "grep: invalid option -- '%c'\n", arg[c]);
                        return 2;
                }
            }
            i++;
            continue;
        }

        if (raw_patterns.count == 0) {
            add_pattern(&raw_patterns, arg);
        } else {
            if (file_count < 256) {
                files[file_count++] = arg;
            }
        }
        i++;
    }

    if (raw_patterns.count == 0) {
        fprintf(stderr, "Usage: grep [OPTION]... PATTERNS [FILE]...\n");
        return 2;
    }

    /* Compile regular expressions if not in fixed string mode */
    regex_t regexes[16];
    int regex_count = 0;
    if (!opt_fixed_strings) {
        int cflags = 0;
        if (opt_extended_regexp) cflags |= REG_EXTENDED;
        if (opt_ignore_case) cflags |= REG_ICASE;

        for (int p = 0; p < raw_patterns.count && regex_count < 16; ++p) {
            if (regcomp(&regexes[regex_count], raw_patterns.patterns[p], cflags) == 0) {
                regex_count++;
            } else {
                fprintf(stderr, "grep: invalid regular expression: %s\n", raw_patterns.patterns[p]);
                free_patterns(&raw_patterns);
                return 2;
            }
        }
    }

    int total_matched = 0;
    int had_error = 0;

    if (file_count == 0) {
        int show = (opt_with_filename == 1);
        int m = grep_stream(STDIN_FILENO, "(standard input)", &raw_patterns, regexes, regex_count, show);
        if (m < 0) had_error = 1;
        else total_matched += m;
    } else {
        int show = (opt_with_filename == 1) || (opt_with_filename == -1 && (file_count > 1 || opt_recursive));
        for (int f = 0; f < file_count; ++f) {
            int m = grep_target(files[f], &raw_patterns, regexes, regex_count, show);
            if (m < 0) had_error = 1;
            else total_matched += m;
            if (opt_quiet && total_matched > 0) break;
        }
    }

    if (!opt_fixed_strings) {
        for (int p = 0; p < regex_count; ++p) {
            regfree(&regexes[p]);
        }
    }
    free_patterns(&raw_patterns);

    if (had_error && total_matched == 0) return 2;
    return total_matched > 0 ? 0 : 1;
}
