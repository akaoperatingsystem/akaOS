#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PATH 512
#define MAX_LINE 2048
#define MAX_KEYWORDS 32

/* Options flags */
static int opt_and = 0;         /* -a, --and */
static int opt_exact = 0;       /* -e, --exact */
static int opt_regex = 1;       /* -r, --regex (default) */
static int opt_wildcard = 0;    /* -w, --wildcard */
static int opt_long = 0;        /* -l, --long */
static int opt_debug = 0;       /* -d, --debug */
static int opt_verbose = 0;     /* -v, --verbose */
static char *opt_sections = NULL;  /* -s, --sections, --section */
static char *opt_systems = NULL;   /* -m, --systems */
static char *opt_manpath = NULL;   /* -M, --manpath */
static char *opt_locale = NULL;    /* -L, --locale */
static char *opt_config = NULL;    /* -C, --config-file */

static void print_usage(void) {
    printf("Usage: apropos [-dalv?V] [-e|-r|-w] [-s list] [-m system[,...]] [-M path] [-L locale] [-C file] keyword ...\n");
}

static void print_help(void) {
    printf("Usage: apropos [OPTION...] KEYWORD...\n"
           "Search the manual page names and descriptions, whatis database.\n\n"
           "Options:\n"
           "  -a, --and                  require all keywords to match\n"
           "  -C, --config-file=FILE     use this user configuration file\n"
           "  -d, --debug                emit debugging messages\n"
           "  -e, --exact                search each keyword for exact match\n"
           "  -r, --regex                interpret each keyword as a regex (default)\n"
           "  -w, --wildcard             the keyword(s) contain wildcards\n"
           "  -l, --long                 do not trim output to terminal width\n"
           "  -m, --systems=SYSTEM       use manual pages from other systems\n"
           "  -M, --manpath=PATH         set search path for manual pages to PATH\n"
           "  -L, --locale=LOCALE        define the locale for this search\n"
           "  -s, --sections=LIST, --section=LIST\n"
           "                             search only these sections (colon- or comma-separated)\n"
           "  -v, --verbose              print verbose warning messages\n"
           "  -?, --help                 give this help list\n"
           "      --usage                give a short usage message\n"
           "  -V, --version              print program version\n\n"
           "Exit Status:\n"
           "  0   Successful program execution.\n"
           "  1   Usage, syntax or configuration file error.\n"
           "  2   Operational error.\n"
           "  16  Nothing was found that matched the criteria specified.\n");
}

static void print_version(void) {
    printf("apropos (akaOS manual pager suite) 0.2.30\n");
}

static int get_terminal_width(void) {
    const char *width_env = getenv("MANWIDTH");
    if (width_env && *width_env) {
        int w = atoi(width_env);
        if (w > 20) return w;
    }
    const char *col_env = getenv("COLUMNS");
    if (col_env && *col_env) {
        int w = atoi(col_env);
        if (w > 20) return w;
    }
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) {
        return (int)ws.ws_col;
    }
    return 80;
}

static int str_case_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return 1;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; ++i) {
        size_t j = 0;
        while (j < nlen) {
            char c1 = haystack[i + j];
            char c2 = needle[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + ('a' - 'A'));
            if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + ('a' - 'A'));
            if (c1 != c2) break;
            ++j;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static int wildcard_match_char(const char *pattern, const char *text) {
    if (!pattern || !text) return 0;
    if (!*pattern) return *text == '\0';
    if (*pattern == '*') {
        while (pattern[1] == '*') pattern++;
        if (wildcard_match_char(pattern + 1, text)) return 1;
        return *text != '\0' && wildcard_match_char(pattern, text + 1);
    }
    if (*pattern == '?') {
        return *text != '\0' && wildcard_match_char(pattern + 1, text + 1);
    }
    char c1 = *pattern;
    char c2 = *text;
    if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + ('a' - 'A'));
    if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + ('a' - 'A'));
    return (c1 == c2) && wildcard_match_char(pattern + 1, text + 1);
}

static int wildcard_match_substring(const char *pattern, const char *text) {
    if (!pattern || !text) return 0;
    /* If pattern contains wildcards, test at all substring offsets */
    size_t tlen = strlen(text);
    for (size_t i = 0; i < tlen; ++i) {
        if (wildcard_match_char(pattern, text + i)) return 1;
    }
    return wildcard_match_char(pattern, text);
}

/* Check if the line matches section filter */
static int section_matches(const char *sec, const char *filter_list) {
    if (!filter_list || !*filter_list) return 1;
    char buf[128];
    strncpy(buf, filter_list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, ":, ");
    while (token) {
        if (strcmp(sec, token) == 0) return 1;
        /* Prefix match: section "3" matches "3", "3perl", "3x" */
        size_t tlen = strlen(token);
        if (strncmp(sec, token, tlen) == 0) return 1;
        token = strtok(NULL, ":, ");
    }
    return 0;
}

/* Parse a whatis entry line: "name (sec) - description" or "name [ext] (sec) - description" */
static int parse_whatis_line(const char *line, char *name_out, size_t name_sz,
                             char *sec_out, size_t sec_sz,
                             char *desc_out, size_t desc_sz) {
    const char *dash = strstr(line, " - ");
    if (!dash) return 0;

    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\0') return 0;

    const char *paren_open = strchr(p, '(');
    const char *paren_close = strchr(p, ')');
    if (!paren_open || !paren_close || paren_open > dash || paren_close > dash) {
        return 0;
    }

    size_t nlen = (size_t)(paren_open - p);
    while (nlen > 0 && (p[nlen - 1] == ' ' || p[nlen - 1] == '\t')) --nlen;
    if (nlen >= name_sz) nlen = name_sz - 1;
    memcpy(name_out, p, nlen);
    name_out[nlen] = '\0';

    size_t slen = (size_t)(paren_close - (paren_open + 1));
    if (slen >= sec_sz) slen = sec_sz - 1;
    memcpy(sec_out, paren_open + 1, slen);
    sec_out[slen] = '\0';

    const char *desc = dash + 3;
    while (*desc == ' ' || *desc == '\t') desc++;
    size_t dlen = strlen(desc);
    while (dlen > 0 && (desc[dlen - 1] == '\r' || desc[dlen - 1] == '\n' ||
                         desc[dlen - 1] == ' ' || desc[dlen - 1] == '\t')) {
        --dlen;
    }
    if (dlen >= desc_sz) dlen = desc_sz - 1;
    memcpy(desc_out, desc, dlen);
    desc_out[dlen] = '\0';

    return 1;
}

static int match_single_keyword(const char *keyword, regex_t *preg,
                                const char *name, const char *desc) {
    if (opt_exact) {
        if (strcasecmp(name, keyword) == 0) return 1;
        if (strcasecmp(desc, keyword) == 0) return 1;
        return 0;
    }

    if (opt_wildcard) {
        if (wildcard_match_substring(keyword, name)) return 1;
        if (wildcard_match_substring(keyword, desc)) return 1;
        return 0;
    }

    if (opt_regex) {
        if (preg) {
            if (regexec(preg, name, 0, NULL, 0) == 0) return 1;
            if (regexec(preg, desc, 0, NULL, 0) == 0) return 1;
            return 0;
        }
    }

    /* Fallback plain case-insensitive substring search */
    if (str_case_contains(name, keyword)) return 1;
    if (str_case_contains(desc, keyword)) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    char *keywords[MAX_KEYWORDS];
    int keyword_count = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-?") == 0 || strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "--usage") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--and") == 0) {
            opt_and = 1;
            continue;
        }
        if (strcmp(arg, "-d") == 0 || strcmp(arg, "--debug") == 0) {
            opt_debug = 1;
            continue;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opt_verbose = 1;
            continue;
        }
        if (strcmp(arg, "-e") == 0 || strcmp(arg, "--exact") == 0) {
            opt_exact = 1;
            opt_regex = 0;
            opt_wildcard = 0;
            continue;
        }
        if (strcmp(arg, "-r") == 0 || strcmp(arg, "--regex") == 0) {
            opt_regex = 1;
            opt_exact = 0;
            opt_wildcard = 0;
            continue;
        }
        if (strcmp(arg, "-w") == 0 || strcmp(arg, "--wildcard") == 0) {
            opt_wildcard = 1;
            opt_exact = 0;
            opt_regex = 0;
            continue;
        }
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "--long") == 0) {
            opt_long = 1;
            continue;
        }
        if (strncmp(arg, "-s", 2) == 0 || strncmp(arg, "--sections=", 11) == 0 ||
            strncmp(arg, "--section=", 10) == 0) {
            if (strncmp(arg, "--sections=", 11) == 0) opt_sections = arg + 11;
            else if (strncmp(arg, "--section=", 10) == 0) opt_sections = arg + 10;
            else if (arg[2] != '\0') opt_sections = arg + 2;
            else if (i + 1 < argc) opt_sections = argv[++i];
            continue;
        }
        if (strncmp(arg, "-m", 2) == 0 || strncmp(arg, "--systems=", 10) == 0) {
            if (strncmp(arg, "--systems=", 10) == 0) opt_systems = arg + 10;
            else if (arg[2] != '\0') opt_systems = arg + 2;
            else if (i + 1 < argc) opt_systems = argv[++i];
            continue;
        }
        if (strncmp(arg, "-M", 2) == 0 || strncmp(arg, "--manpath=", 10) == 0) {
            if (strncmp(arg, "--manpath=", 10) == 0) opt_manpath = arg + 10;
            else if (arg[2] != '\0') opt_manpath = arg + 2;
            else if (i + 1 < argc) opt_manpath = argv[++i];
            continue;
        }
        if (strncmp(arg, "-L", 2) == 0 || strncmp(arg, "--locale=", 9) == 0) {
            if (strncmp(arg, "--locale=", 9) == 0) opt_locale = arg + 9;
            else if (arg[2] != '\0') opt_locale = arg + 2;
            else if (i + 1 < argc) opt_locale = argv[++i];
            continue;
        }
        if (strncmp(arg, "-C", 2) == 0 || strncmp(arg, "--config-file=", 14) == 0) {
            if (strncmp(arg, "--config-file=", 14) == 0) opt_config = arg + 14;
            else if (arg[2] != '\0') opt_config = arg + 2;
            else if (i + 1 < argc) opt_config = argv[++i];
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "apropos: unrecognized option '%s'\n", arg);
            fprintf(stderr, "Try 'apropos --help' or 'apropos --usage' for more information.\n");
            return 1;
        }

        if (keyword_count < MAX_KEYWORDS) {
            keywords[keyword_count++] = arg;
        }
    }

    if (keyword_count == 0) {
        fprintf(stderr, "apropos what?\n");
        return 1;
    }

    if (opt_debug) {
        fprintf(stderr, "[debug] keyword_count=%d, and=%d, exact=%d, regex=%d, wildcard=%d\n",
                keyword_count, opt_and, opt_exact, opt_regex, opt_wildcard);
    }

    /* Compile regexes if in regex mode */
    regex_t pregs[MAX_KEYWORDS];
    int preg_valid[MAX_KEYWORDS];
    memset(preg_valid, 0, sizeof(preg_valid));

    if (opt_regex) {
        for (int k = 0; k < keyword_count; ++k) {
            if (regcomp(&pregs[k], keywords[k], REG_EXTENDED | REG_ICASE) == 0) {
                preg_valid[k] = 1;
            } else {
                if (opt_verbose || opt_debug) {
                    fprintf(stderr, "apropos: warning: regex compilation failed for '%s'\n", keywords[k]);
                }
            }
        }
    }

    /* Determine whatis search paths */
    const char *search_paths[8];
    int num_paths = 0;

    if (opt_manpath && *opt_manpath) {
        search_paths[num_paths++] = opt_manpath;
    } else {
        const char *mp_env = getenv("MANPATH");
        if (mp_env && *mp_env) {
            search_paths[num_paths++] = mp_env;
        } else {
            search_paths[num_paths++] = "/usr/share/man";
            search_paths[num_paths++] = "/var/cache/man";
            search_paths[num_paths++] = "/usr/local/share/man";
            search_paths[num_paths++] = "/etc/man";
        }
    }

    int match_found = 0;
    int terminal_width = get_terminal_width();

    /* Search across whatis database files in paths */
    for (int p = 0; p < num_paths; ++p) {
        char file_path[MAX_PATH];
        const char *base = search_paths[p];
        if (strstr(base, "whatis")) {
            strncpy(file_path, base, sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
        } else {
            snprintf(file_path, sizeof(file_path), "%s/whatis", base);
        }

        if (opt_debug) {
            fprintf(stderr, "[debug] probing database file: %s\n", file_path);
        }

        FILE *f = fopen(file_path, "r");
        if (!f) continue;

        char line[MAX_LINE];
        while (fgets(line, sizeof(line), f)) {
            char name[128], sec[32], desc[MAX_LINE];
            if (!parse_whatis_line(line, name, sizeof(name), sec, sizeof(sec), desc, sizeof(desc))) {
                continue;
            }

            if (opt_sections && !section_matches(sec, opt_sections)) {
                continue;
            }

            int matched = 0;
            if (opt_and) {
                matched = 1;
                for (int k = 0; k < keyword_count; ++k) {
                    regex_t *p_re = preg_valid[k] ? &pregs[k] : NULL;
                    if (!match_single_keyword(keywords[k], p_re, name, desc)) {
                        matched = 0;
                        break;
                    }
                }
            } else {
                matched = 0;
                for (int k = 0; k < keyword_count; ++k) {
                    regex_t *p_re = preg_valid[k] ? &pregs[k] : NULL;
                    if (match_single_keyword(keywords[k], p_re, name, desc)) {
                        matched = 1;
                        break;
                    }
                }
            }

            if (matched) {
                match_found = 1;
                char out_buf[MAX_LINE + 128];
                snprintf(out_buf, sizeof(out_buf), "%-20s (%s) - %s", name, sec, desc);

                if (!opt_long && (int)strlen(out_buf) > terminal_width && terminal_width > 24) {
                    out_buf[terminal_width] = '\0';
                }
                printf("%s\n", out_buf);
            }
        }
        fclose(f);
    }

    if (opt_regex) {
        for (int k = 0; k < keyword_count; ++k) {
            if (preg_valid[k]) regfree(&pregs[k]);
        }
    }

    if (!match_found) {
        for (int k = 0; k < keyword_count; ++k) {
            printf("%s: nothing appropriate.\n", keywords[k]);
        }
        return 16;
    }

    return 0;
}
