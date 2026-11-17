#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_HISTSIZE 500
#define DEFAULT_HISTFILESIZE 500
#define MAX_HIST_ENTRIES 16
#define MAX_LINE_LEN 128

struct hist_entry {
    char line[MAX_LINE_LEN];
    time_t timestamp;
    int modified;
};

static struct hist_entry history_list[MAX_HIST_ENTRIES];
static int history_count = 0;
static int session_start_count = 0;

static const char *get_default_histfile(void) {
    const char *hf = getenv("HISTFILE");
    if (hf && *hf) return hf;
    const char *home = getenv("HOME");
    static char path[512];
    if (home && *home) {
        snprintf(path, sizeof(path), "%s/.bash_history", home);
    } else {
        snprintf(path, sizeof(path), "/.bash_history");
    }
    return path;
}

static int get_histsize(void) {
    const char *sz = getenv("HISTSIZE");
    if (sz && *sz) {
        int n = atoi(sz);
        if (n > 0) return (n > MAX_HIST_ENTRIES) ? MAX_HIST_ENTRIES : n;
    }
    return DEFAULT_HISTSIZE;
}

static int get_histfilesize(void) {
    const char *sz = getenv("HISTFILESIZE");
    if (sz && *sz) {
        int n = atoi(sz);
        if (n > 0) return n;
    }
    return DEFAULT_HISTFILESIZE;
}

static void print_help(void) {
    puts("history: history [-c] [-d offset] [n] or history -anrw [filename] or history -ps arg [arg...]");
    puts("    Display or manipulate the history list.");
    puts("");
    puts("    Display the history list with line numbers. Lines listed with");
    puts("    a '*' have been modified.");
    puts("");
    puts("    Options:");
    puts("      -c        clear the history list");
    puts("      -d offset delete the history entry at position OFFSET");
    puts("      -a        append history lines from this session to the history file");
    puts("      -n        read all history lines not already read from the history file");
    puts("      -r        read the history file and append the contents to the history");
    puts("                list");
    puts("      -w        write the current history to the history file");
    puts("      -p        perform history expansion on each ARG and display the result");
    puts("                without storing it in the history list");
    puts("      -s        append the ARGs to the history list as a single entry");
    puts("      -h, --help display this help and exit");
}

static void add_history_entry(const char *cmd, time_t t, int mod) {
    if (!cmd || !*cmd) return;
    int limit = get_histsize();
    if (history_count >= limit || history_count >= MAX_HIST_ENTRIES) {
        for (int i = 0; i < history_count - 1; ++i) {
            history_list[i] = history_list[i + 1];
        }
        history_count--;
        if (session_start_count > 0) session_start_count--;
    }
    struct hist_entry *e = &history_list[history_count++];
    strncpy(e->line, cmd, sizeof(e->line) - 1);
    e->line[sizeof(e->line) - 1] = '\0';
    e->timestamp = t ? t : time(NULL);
    e->modified = mod;
}

static int read_history_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    char buf[MAX_LINE_LEN];
    time_t cur_time = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
            buf[--len] = '\0';
        }
        if (len == 0) continue;
        if (buf[0] == '#' && isdigit((unsigned char)buf[1])) {
            cur_time = (time_t)atol(buf + 1);
            continue;
        }
        add_history_entry(buf, cur_time, 0);
        cur_time = 0;
    }
    fclose(f);
    return 0;
}

static int write_history_file(const char *filename, int append_only, int start_idx) {
    const char *mode = append_only ? "a" : "w";
    FILE *f = fopen(filename, mode);
    if (!f) {
        fprintf(stderr, "history: %s: cannot open history file: %s\n", filename, strerror(errno));
        return -1;
    }
    int max_entries = get_histfilesize();
    int begin = start_idx;
    if (!append_only && (history_count - begin) > max_entries) {
        begin = history_count - max_entries;
    }
    for (int i = begin; i < history_count; ++i) {
        if (history_list[i].timestamp > 0) {
            fprintf(f, "#%ld\n", (long)history_list[i].timestamp);
        }
        fprintf(f, "%s\n", history_list[i].line);
    }
    fclose(f);
    return 0;
}

static void display_history(int num_lines) {
    int start = 0;
    if (num_lines > 0 && num_lines < history_count) {
        start = history_count - num_lines;
    }
    const char *time_fmt = getenv("HISTTIMEFORMAT");
    char time_str[128];
    for (int i = start; i < history_count; ++i) {
        time_str[0] = '\0';
        if (time_fmt && *time_fmt && history_list[i].timestamp > 0) {
            struct tm *tm_info = localtime(&history_list[i].timestamp);
            if (tm_info) {
                strftime(time_str, sizeof(time_str), time_fmt, tm_info);
            }
        }
        printf("%5d%c %s%s\n",
               i + 1,
               history_list[i].modified ? '*' : ' ',
               time_str,
               history_list[i].line);
    }
}

static int delete_history_entry(int offset) {
    if (offset < 1 || offset > history_count) {
        fprintf(stderr, "history: %d: history position out of range\n", offset);
        return -1;
    }
    int idx = offset - 1;
    for (int i = idx; i < history_count - 1; ++i) {
        history_list[i] = history_list[i + 1];
    }
    history_count--;
    return 0;
}

static int perform_expansion(const char *in, char *out, size_t out_max) {
    if (!in || !*in) {
        if (out_max > 0) out[0] = '\0';
        return 0;
    }
    size_t o = 0;
    size_t len = strlen(in);
    size_t i = 0;

    if (in[0] == '^') {
        const char *p2 = strchr(in + 1, '^');
        if (p2) {
            char old_str[128];
            char new_str[128];
            size_t old_len = (size_t)(p2 - (in + 1));
            if (old_len >= sizeof(old_str)) old_len = sizeof(old_str) - 1;
            strncpy(old_str, in + 1, old_len);
            old_str[old_len] = '\0';

            const char *p3 = strchr(p2 + 1, '^');
            size_t new_len = p3 ? (size_t)(p3 - (p2 + 1)) : strlen(p2 + 1);
            if (new_len >= sizeof(new_str)) new_len = sizeof(new_str) - 1;
            strncpy(new_str, p2 + 1, new_len);
            new_str[new_len] = '\0';

            if (history_count == 0) {
                fprintf(stderr, "history: !!: event not found\n");
                return -1;
            }
            const char *last_cmd = history_list[history_count - 1].line;
            char *found = strstr(last_cmd, old_str);
            if (!found) {
                fprintf(stderr, "history: %s: substitution failed\n", old_str);
                return -1;
            }
            size_t prefix_len = (size_t)(found - last_cmd);
            snprintf(out, out_max, "%.*s%s%s%s",
                     (int)prefix_len, last_cmd,
                     new_str,
                     found + strlen(old_str),
                     p3 ? p3 + 1 : "");
            return 0;
        }
    }

    while (i < len) {
        if (in[i] == '\\' && i + 1 < len && in[i + 1] == '!') {
            if (o + 1 < out_max) out[o++] = '!';
            i += 2;
            continue;
        }
        if (in[i] == '!' && i + 1 < len &&
            in[i + 1] != ' ' && in[i + 1] != '\t' && in[i + 1] != '\n' &&
            in[i + 1] != '=' && in[i + 1] != '(') {
            i++;
            const char *event_line = NULL;

            if (in[i] == '!') {
                if (history_count == 0) {
                    fprintf(stderr, "history: !!: event not found\n");
                    return -1;
                }
                event_line = history_list[history_count - 1].line;
                i++;
            } else if (in[i] == '-') {
                i++;
                int n = 0;
                while (i < len && isdigit((unsigned char)in[i])) {
                    n = n * 10 + (in[i++] - '0');
                }
                if (n <= 0 || n > history_count) {
                    fprintf(stderr, "history: !-%d: event not found\n", n);
                    return -1;
                }
                event_line = history_list[history_count - n].line;
            } else if (isdigit((unsigned char)in[i])) {
                int n = 0;
                while (i < len && isdigit((unsigned char)in[i])) {
                    n = n * 10 + (in[i++] - '0');
                }
                if (n <= 0 || n > history_count) {
                    fprintf(stderr, "history: !%d: event not found\n", n);
                    return -1;
                }
                event_line = history_list[n - 1].line;
            } else if (in[i] == '?') {
                i++;
                char pat[128];
                int pat_len = 0;
                while (i < len && in[i] != '?' && in[i] != '\n' && in[i] != ':' && in[i] != ' ') {
                    if (pat_len < (int)sizeof(pat) - 1) pat[pat_len++] = in[i];
                    i++;
                }
                pat[pat_len] = '\0';
                if (i < len && in[i] == '?') i++;
                for (int h = history_count - 1; h >= 0; --h) {
                    if (strstr(history_list[h].line, pat)) {
                        event_line = history_list[h].line;
                        break;
                    }
                }
                if (!event_line) {
                    fprintf(stderr, "history: !?%s?: event not found\n", pat);
                    return -1;
                }
            } else if (in[i] == '#') {
                i++;
                out[o] = '\0';
                event_line = out;
            } else {
                char pat[128];
                int pat_len = 0;
                while (i < len && !isspace((unsigned char)in[i]) && in[i] != ':' &&
                       in[i] != '^' && in[i] != '$' && in[i] != '*') {
                    if (pat_len < (int)sizeof(pat) - 1) pat[pat_len++] = in[i];
                    i++;
                }
                pat[pat_len] = '\0';
                for (int h = history_count - 1; h >= 0; --h) {
                    if (strncmp(history_list[h].line, pat, strlen(pat)) == 0) {
                        event_line = history_list[h].line;
                        break;
                    }
                }
                if (!event_line) {
                    fprintf(stderr, "history: !%s: event not found\n", pat);
                    return -1;
                }
            }

            char words[64][128];
            int nwords = 0;
            const char *src = event_line;
            while (*src && nwords < 64) {
                while (*src && isspace((unsigned char)*src)) src++;
                if (!*src) break;
                char *dst = words[nwords];
                int wlen = 0;
                int q = 0;
                while (*src) {
                    if ((*src == '\'' || *src == '"') && (!q || q == *src)) {
                        q = q ? 0 : *src;
                        src++;
                        continue;
                    }
                    if (!q && isspace((unsigned char)*src)) break;
                    if (wlen < 127) dst[wlen++] = *src;
                    src++;
                }
                dst[wlen] = '\0';
                nwords++;
            }

            int start_w = 0;
            int end_w = nwords - 1;
            int have_word_spec = 0;

            if (i < len && in[i] == ':') {
                i++;
            }

            if (i < len && (in[i] == '^' || in[i] == '$' || in[i] == '*' ||
                            in[i] == '-' || isdigit((unsigned char)in[i]))) {
                have_word_spec = 1;
                if (in[i] == '^') {
                    start_w = end_w = (nwords > 1 ? 1 : 0);
                    i++;
                } else if (in[i] == '$') {
                    start_w = end_w = (nwords > 0 ? nwords - 1 : 0);
                    i++;
                } else if (in[i] == '*') {
                    start_w = 1;
                    end_w = nwords - 1;
                    i++;
                } else if (isdigit((unsigned char)in[i])) {
                    int wnum = 0;
                    while (i < len && isdigit((unsigned char)in[i])) {
                        wnum = wnum * 10 + (in[i++] - '0');
                    }
                    start_w = end_w = wnum;
                    if (i < len && in[i] == '-') {
                        i++;
                        if (i < len && isdigit((unsigned char)in[i])) {
                            int wend = 0;
                            while (i < len && isdigit((unsigned char)in[i])) {
                                wend = wend * 10 + (in[i++] - '0');
                            }
                            end_w = wend;
                        } else {
                            end_w = nwords - 2;
                        }
                    } else if (i < len && in[i] == '*') {
                        i++;
                        end_w = nwords - 1;
                    }
                }
            }

            char expanded_event[1024];
            size_t elen = 0;
            expanded_event[0] = '\0';

            if (start_w >= 0 && start_w < nwords) {
                if (end_w >= nwords) end_w = nwords - 1;
                for (int w = start_w; w <= end_w; ++w) {
                    if (elen > 0 && elen + 1 < sizeof(expanded_event)) {
                        expanded_event[elen++] = ' ';
                    }
                    size_t wl = strlen(words[w]);
                    if (elen + wl < sizeof(expanded_event)) {
                        memcpy(expanded_event + elen, words[w], wl);
                        elen += wl;
                    }
                }
                expanded_event[elen] = '\0';
            } else if (!have_word_spec) {
                strncpy(expanded_event, event_line, sizeof(expanded_event) - 1);
                expanded_event[sizeof(expanded_event) - 1] = '\0';
            }

            while (i < len && in[i] == ':') {
                i++;
                char mod = in[i++];
                if (mod == 'h') {
                    char *slash = strrchr(expanded_event, '/');
                    if (slash) *slash = '\0';
                } else if (mod == 't') {
                    char *slash = strrchr(expanded_event, '/');
                    if (slash) {
                        memmove(expanded_event, slash + 1, strlen(slash + 1) + 1);
                    }
                } else if (mod == 'r') {
                    char *dot = strrchr(expanded_event, '.');
                    if (dot) *dot = '\0';
                } else if (mod == 'e') {
                    char *dot = strrchr(expanded_event, '.');
                    if (dot) {
                        memmove(expanded_event, dot, strlen(dot) + 1);
                    } else {
                        expanded_event[0] = '\0';
                    }
                } else if (mod == 's') {
                    char delim = (i < len) ? in[i++] : '/';
                    char old_sub[128];
                    char new_sub[128];
                    int slen = 0;
                    while (i < len && in[i] != delim) {
                        if (slen < (int)sizeof(old_sub) - 1) old_sub[slen++] = in[i];
                        i++;
                    }
                    old_sub[slen] = '\0';
                    if (i < len && in[i] == delim) i++;
                    slen = 0;
                    while (i < len && in[i] != delim) {
                        if (slen < (int)sizeof(new_sub) - 1) new_sub[slen++] = in[i];
                        i++;
                    }
                    new_sub[slen] = '\0';
                    if (i < len && in[i] == delim) i++;

                    char *target = strstr(expanded_event, old_sub);
                    if (target) {
                        char tmp[1024];
                        size_t pfx = (size_t)(target - expanded_event);
                        snprintf(tmp, sizeof(tmp), "%.*s%s%s",
                                 (int)pfx, expanded_event,
                                 new_sub,
                                 target + strlen(old_sub));
                        strncpy(expanded_event, tmp, sizeof(expanded_event) - 1);
                        expanded_event[sizeof(expanded_event) - 1] = '\0';
                    }
                }
            }

            size_t evl = strlen(expanded_event);
            if (o + evl < out_max) {
                memcpy(out + o, expanded_event, evl);
                o += evl;
            }
            continue;
        }

        if (o + 1 < out_max) {
            out[o++] = in[i++];
        } else {
            i++;
        }
    }
    out[o] = '\0';
    return 0;
}

int main(int argc, char **argv) {
    const char *histfile = get_default_histfile();

    if (history_count == 0) {
        read_history_file(histfile);
        session_start_count = history_count;
    }

    if (argc == 1) {
        display_history(0);
        return 0;
    }

    if (argc == 2 && isdigit((unsigned char)argv[1][0])) {
        int n = atoi(argv[1]);
        display_history(n);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "-c") == 0) {
            history_count = 0;
            session_start_count = 0;
            continue;
        }
        if (strcmp(arg, "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "history: -d: option requires an argument\n");
                return 2;
            }
            int offset = atoi(argv[++i]);
            if (delete_history_entry(offset) != 0) return 1;
            continue;
        }
        if (strcmp(arg, "-a") == 0) {
            const char *file = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : histfile;
            if (write_history_file(file, 1, session_start_count) != 0) return 1;
            session_start_count = history_count;
            continue;
        }
        if (strcmp(arg, "-n") == 0) {
            const char *file = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : histfile;
            if (read_history_file(file) != 0) return 1;
            session_start_count = history_count;
            continue;
        }
        if (strcmp(arg, "-r") == 0) {
            const char *file = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : histfile;
            if (read_history_file(file) != 0) return 1;
            session_start_count = history_count;
            continue;
        }
        if (strcmp(arg, "-w") == 0) {
            const char *file = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : histfile;
            if (write_history_file(file, 0, 0) != 0) return 1;
            continue;
        }
        if (strcmp(arg, "-p") == 0) {
            int had_err = 0;
            for (int k = i + 1; k < argc; ++k) {
                char expanded[1024];
                if (perform_expansion(argv[k], expanded, sizeof(expanded)) == 0) {
                    printf("%s\n", expanded);
                } else {
                    had_err = 1;
                }
            }
            return had_err ? 1 : 0;
        }
        if (strcmp(arg, "-s") == 0) {
            char combined[1024];
            size_t c_len = 0;
            combined[0] = '\0';
            for (int k = i + 1; k < argc; ++k) {
                if (c_len > 0 && c_len + 1 < sizeof(combined)) {
                    combined[c_len++] = ' ';
                }
                size_t l = strlen(argv[k]);
                if (c_len + l < sizeof(combined)) {
                    memcpy(combined + c_len, argv[k], l);
                    c_len += l;
                }
            }
            combined[c_len] = '\0';
            if (c_len > 0) {
                add_history_entry(combined, time(NULL), 0);
            }
            return 0;
        }
        fprintf(stderr, "history: %s: invalid option\n", arg);
        print_help();
        return 2;
    }

    return 0;
}
