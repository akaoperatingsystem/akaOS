#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_BINDINGS 128
#define MAX_VARIABLES 32

struct key_binding {
    char keyseq[32];
    char function[64];
    char macro[64];
    char shell_cmd[128];
    int is_macro;
    int is_shell_cmd;
};

struct readline_var {
    char name[32];
    char value[32];
};

static const char *rl_functions[] = {
    "abort",
    "accept-line",
    "alias-expand-line",
    "arrow-key-prefix",
    "backward-byte",
    "backward-char",
    "backward-delete-char",
    "backward-kill-line",
    "backward-kill-word",
    "backward-word",
    "beginning-of-history",
    "beginning-of-line",
    "call-last-kbd-macro",
    "capitalize-word",
    "character-search",
    "character-search-backward",
    "clear-display",
    "clear-screen",
    "complete",
    "complete-command",
    "complete-filename",
    "complete-hostname",
    "complete-into-braces",
    "complete-username",
    "complete-variable",
    "copy-backward-word",
    "copy-forward-word",
    "copy-region-as-kill",
    "delete-char",
    "delete-char-or-list",
    "delete-horizontal-space",
    "digit-argument",
    "do-lowercase-version",
    "downcase-word",
    "dump-functions",
    "dump-macros",
    "dump-variables",
    "emacs-editing-mode",
    "end-of-history",
    "end-of-line",
    "exchange-point-and-mark",
    "forward-backward-delete-char",
    "forward-byte",
    "forward-char",
    "forward-search-history",
    "forward-word",
    "history-and-alias-expand-line",
    "history-expand-line",
    "history-search-backward",
    "history-search-forward",
    "history-substr-search-backward",
    "history-substr-search-forward",
    "kill-line",
    "kill-region",
    "kill-whole-line",
    "kill-word",
    "next-history",
    "non-incremental-forward-search-history",
    "non-incremental-reverse-search-history",
    "previous-history",
    "quoted-insert",
    "re-read-init-file",
    "redraw-current-line",
    "reverse-search-history",
    "self-insert",
    "set-mark",
    "transpose-chars",
    "transpose-words",
    "undo",
    "universal-argument",
    "unix-filename-rubout",
    "unix-line-discard",
    "unix-word-rubout",
    "upcase-word",
    "vi-editing-mode",
    "yank",
    "yank-last-arg",
    "yank-nth-arg",
    "yank-pop"
};
static const int num_rl_functions = sizeof(rl_functions) / sizeof(rl_functions[0]);

static struct key_binding default_bindings[] = {
    {"\\C-g", "abort", "", "", 0, 0},
    {"\\C-j", "accept-line", "", "", 0, 0},
    {"\\C-m", "accept-line", "", "", 0, 0},
    {"\\C-b", "backward-char", "", "", 0, 0},
    {"\\e[D", "backward-char", "", "", 0, 0},
    {"\\eOD", "backward-char", "", "", 0, 0},
    {"\\C-f", "forward-char", "", "", 0, 0},
    {"\\e[C", "forward-char", "", "", 0, 0},
    {"\\eOC", "forward-char", "", "", 0, 0},
    {"\\C-p", "previous-history", "", "", 0, 0},
    {"\\e[A", "previous-history", "", "", 0, 0},
    {"\\eOA", "previous-history", "", "", 0, 0},
    {"\\C-n", "next-history", "", "", 0, 0},
    {"\\e[B", "next-history", "", "", 0, 0},
    {"\\eOB", "next-history", "", "", 0, 0},
    {"\\C-a", "beginning-of-line", "", "", 0, 0},
    {"\\C-e", "end-of-line", "", "", 0, 0},
    {"\\C-d", "delete-char", "", "", 0, 0},
    {"\\C-h", "backward-delete-char", "", "", 0, 0},
    {"\\C-?", "backward-delete-char", "", "", 0, 0},
    {"\\C-k", "kill-line", "", "", 0, 0},
    {"\\C-u", "unix-line-discard", "", "", 0, 0},
    {"\\C-w", "unix-word-rubout", "", "", 0, 0},
    {"\\C-y", "yank", "", "", 0, 0},
    {"\\C-l", "clear-screen", "", "", 0, 0},
    {"\\C-r", "reverse-search-history", "", "", 0, 0},
    {"\\C-s", "forward-search-history", "", "", 0, 0},
    {"\\C-t", "transpose-chars", "", "", 0, 0},
    {"\\C-_", "undo", "", "", 0, 0},
    {"\\C-v", "quoted-insert", "", "", 0, 0},
    {"\\t", "complete", "", "", 0, 0}
};

static struct key_binding bindings[MAX_BINDINGS];
static int num_bindings = 0;

static struct readline_var variables[] = {
    {"bind-tty-special-chars", "on"},
    {"blink-matching-paren", "off"},
    {"byte-oriented", "off"},
    {"colored-completion-prefix", "off"},
    {"colored-stats", "off"},
    {"completion-ignore-case", "off"},
    {"completion-map-case", "off"},
    {"convert-meta", "off"},
    {"disable-completion", "off"},
    {"echo-control-characters", "on"},
    {"enable-bracketed-paste", "on"},
    {"enable-keypad", "off"},
    {"expand-tilde", "off"},
    {"history-preserve-point", "off"},
    {"horizontal-scroll-mode", "off"},
    {"input-meta", "on"},
    {"mark-directories", "on"},
    {"mark-modified-lines", "off"},
    {"mark-symlinked-directories", "off"},
    {"match-hidden-files", "on"},
    {"menu-complete-display-prefix", "off"},
    {"output-meta", "on"},
    {"page-completions", "on"},
    {"prefer-visible-bell", "on"},
    {"print-completions-horizontally", "off"},
    {"revert-all-at-newline", "off"},
    {"show-all-if-ambiguous", "off"},
    {"show-all-if-unmodified", "off"},
    {"show-mode-in-prompt", "off"},
    {"skip-completed-text", "off"},
    {"visible-stats", "off"}
};
static const int num_variables = sizeof(variables) / sizeof(variables[0]);

static void init_bindings(void) {
    if (num_bindings > 0) return;
    int count = sizeof(default_bindings) / sizeof(default_bindings[0]);
    for (int i = 0; i < count && i < MAX_BINDINGS; ++i) {
        bindings[i] = default_bindings[i];
    }
    num_bindings = count;
}

static void print_help(void) {
    printf("bind: bind [-lpsvPSVX] [-m keymap] [-f filename] [-q name] [-u name] [-r keyseq]\n"
           "           [-x keyseq:shell-command] [keyseq:readline-function or readline-command]\n"
           "    Set or display readline key and function bindings.\n\n"
           "    Bind a key sequence to a readline function or a macro, or set a readline\n"
           "    variable. The non-option argument syntax is that of a readline initialization\n"
           "    file (.inputrc).\n\n"
           "    Options:\n"
           "      -m keymap         Use KEYMAP as the keymap to be affected.\n"
           "      -l                List names of all readline functions.\n"
           "      -p                Display functions and bindings in a re-readable format.\n"
           "      -P                List current readline function names and bindings.\n"
           "      -s                Display key sequences bound to macros in a re-readable format.\n"
           "      -S                Display key sequences bound to macros.\n"
           "      -v                Display readline variable names and values in a re-readable format.\n"
           "      -V                List current readline variable names and values.\n"
           "      -f filename       Read key bindings from FILENAME.\n"
           "      -q function-name  Query which keys invoke FUNCTION-NAME.\n"
           "      -u function-name  Unbind all keys bound to FUNCTION-NAME.\n"
           "      -r keyseq         Remove the binding for KEYSEQ.\n"
           "      -x keyseq:command Cause COMMAND to be executed when KEYSEQ is entered.\n"
           "      -X                List key sequences bound with -x and associated commands.\n");
}

static void list_functions(void) {
    for (int i = 0; i < num_rl_functions; ++i) {
        printf("%s\n", rl_functions[i]);
    }
}

static void list_variables(int readable) {
    for (int i = 0; i < num_variables; ++i) {
        if (readable) {
            printf("set %s %s\n", variables[i].name, variables[i].value);
        } else {
            printf("%s is set to `%s'\n", variables[i].name, variables[i].value);
        }
    }
}

static void query_function(const char *func) {
    int found = 0;
    char buffer[256] = "";
    size_t blen = 0;

    for (int i = 0; i < num_bindings; ++i) {
        if (!bindings[i].is_macro && !bindings[i].is_shell_cmd && strcmp(bindings[i].function, func) == 0) {
            if (found > 0 && blen + 2 < sizeof(buffer)) {
                strcat(buffer, ", ");
                blen += 2;
            }
            char quoted[64];
            snprintf(quoted, sizeof(quoted), "\"%s\"", bindings[i].keyseq);
            if (blen + strlen(quoted) < sizeof(buffer)) {
                strcat(buffer, quoted);
                blen += strlen(quoted);
            }
            found++;
        }
    }

    if (found > 0) {
        printf("%s can be invoked via %s.\n", func, buffer);
    } else {
        printf("%s is not bound to any keys.\n", func);
    }
}

static void unbind_function(const char *func) {
    int dst = 0;
    for (int src = 0; src < num_bindings; ++src) {
        if (!bindings[src].is_macro && !bindings[src].is_shell_cmd && strcmp(bindings[src].function, func) == 0) {
            continue;
        }
        bindings[dst++] = bindings[src];
    }
    num_bindings = dst;
}

static void remove_keyseq(const char *keyseq) {
    int dst = 0;
    for (int src = 0; src < num_bindings; ++src) {
        if (strcmp(bindings[src].keyseq, keyseq) == 0) {
            continue;
        }
        bindings[dst++] = bindings[src];
    }
    num_bindings = dst;
}

static void display_bindings(int readable) {
    if (readable) {
        for (int i = 0; i < num_bindings; ++i) {
            if (!bindings[i].is_macro && !bindings[i].is_shell_cmd) {
                printf("\"%s\": %s\n", bindings[i].keyseq, bindings[i].function);
            }
        }
        for (int f = 0; f < num_rl_functions; ++f) {
            int bound = 0;
            for (int i = 0; i < num_bindings; ++i) {
                if (strcmp(bindings[i].function, rl_functions[f]) == 0) {
                    bound = 1;
                    break;
                }
            }
            if (!bound) {
                printf("# %s (not bound)\n", rl_functions[f]);
            }
        }
    } else {
        for (int f = 0; f < num_rl_functions; ++f) {
            int found = 0;
            char buffer[256] = "";
            size_t blen = 0;
            for (int i = 0; i < num_bindings; ++i) {
                if (!bindings[i].is_macro && !bindings[i].is_shell_cmd && strcmp(bindings[i].function, rl_functions[f]) == 0) {
                    if (found > 0 && blen + 2 < sizeof(buffer)) {
                        strcat(buffer, ", ");
                        blen += 2;
                    }
                    char quoted[64];
                    snprintf(quoted, sizeof(quoted), "\"%s\"", bindings[i].keyseq);
                    if (blen + strlen(quoted) < sizeof(buffer)) {
                        strcat(buffer, quoted);
                        blen += strlen(quoted);
                    }
                    found++;
                }
            }
            if (found > 0) {
                printf("%s can be found on %s.\n", rl_functions[f], buffer);
            } else {
                printf("%s is not bound to any keys\n", rl_functions[f]);
            }
        }
    }
}

static void display_macros(int readable) {
    for (int i = 0; i < num_bindings; ++i) {
        if (bindings[i].is_macro) {
            if (readable) {
                printf("\"%s\": \"%s\"\n", bindings[i].keyseq, bindings[i].macro);
            } else {
                printf("%s outputs %s\n", bindings[i].keyseq, bindings[i].macro);
            }
        }
    }
}

static void display_shell_cmds(void) {
    for (int i = 0; i < num_bindings; ++i) {
        if (bindings[i].is_shell_cmd) {
            printf("\"%s\": \"%s\"\n", bindings[i].keyseq, bindings[i].shell_cmd);
        }
    }
}

static int add_binding(const char *keyseq, const char *func_or_macro, int is_macro, const char *cmd) {
    /* Update existing */
    for (int i = 0; i < num_bindings; ++i) {
        if (strcmp(bindings[i].keyseq, keyseq) == 0) {
            if (cmd) {
                strncpy(bindings[i].shell_cmd, cmd, sizeof(bindings[i].shell_cmd) - 1);
                bindings[i].is_shell_cmd = 1;
                bindings[i].is_macro = 0;
            } else if (is_macro) {
                strncpy(bindings[i].macro, func_or_macro, sizeof(bindings[i].macro) - 1);
                bindings[i].is_macro = 1;
                bindings[i].is_shell_cmd = 0;
            } else {
                strncpy(bindings[i].function, func_or_macro, sizeof(bindings[i].function) - 1);
                bindings[i].is_macro = 0;
                bindings[i].is_shell_cmd = 0;
            }
            return 0;
        }
    }

    if (num_bindings < MAX_BINDINGS) {
        strncpy(bindings[num_bindings].keyseq, keyseq, sizeof(bindings[num_bindings].keyseq) - 1);
        if (cmd) {
            strncpy(bindings[num_bindings].shell_cmd, cmd, sizeof(bindings[num_bindings].shell_cmd) - 1);
            bindings[num_bindings].is_shell_cmd = 1;
            bindings[num_bindings].is_macro = 0;
        } else if (is_macro) {
            strncpy(bindings[num_bindings].macro, func_or_macro, sizeof(bindings[num_bindings].macro) - 1);
            bindings[num_bindings].is_macro = 1;
            bindings[num_bindings].is_shell_cmd = 0;
        } else {
            strncpy(bindings[num_bindings].function, func_or_macro, sizeof(bindings[num_bindings].function) - 1);
            bindings[num_bindings].is_macro = 0;
            bindings[num_bindings].is_shell_cmd = 0;
        }
        num_bindings++;
        return 0;
    }
    return -1;
}

static int parse_binding_line(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#') return 0;

    /* set variable value */
    if (strncmp(line, "set", 3) == 0 && (line[3] == ' ' || line[3] == '\t')) {
        char vname[32], vval[32];
        if (sscanf(line + 3, "%31s %31s", vname, vval) == 2) {
            for (int i = 0; i < num_variables; ++i) {
                if (strcmp(variables[i].name, vname) == 0) {
                    strncpy(variables[i].value, vval, sizeof(variables[i].value) - 1);
                    return 0;
                }
            }
        }
        return 0;
    }

    /* "keyseq": target */
    const char *colon = strchr(line, ':');
    if (!colon) return -1;

    char key[64];
    size_t klen = (size_t)(colon - line);
    if (klen >= sizeof(key)) klen = sizeof(key) - 1;
    memcpy(key, line, klen);
    key[klen] = '\0';

    char *k = key;
    while (*k == ' ' || *k == '\t' || *k == '"' || *k == '\'') k++;
    size_t kl = strlen(k);
    while (kl > 0 && (k[kl - 1] == ' ' || k[kl - 1] == '\t' || k[kl - 1] == '"' || k[kl - 1] == '\'')) {
        k[--kl] = '\0';
    }

    const char *target = colon + 1;
    while (*target == ' ' || *target == '\t') target++;

    char tgt[128];
    strncpy(tgt, target, sizeof(tgt) - 1);
    tgt[sizeof(tgt) - 1] = '\0';
    char *t = tgt;
    while (*t == ' ' || *t == '\t' || *t == '"' || *t == '\'') t++;
    size_t tl = strlen(t);
    while (tl > 0 && (t[tl - 1] == ' ' || t[tl - 1] == '\t' || t[tl - 1] == '"' || t[tl - 1] == '\'')) {
        t[--tl] = '\0';
    }

    int is_mac = (target[0] == '"');
    return add_binding(k, t, is_mac, NULL);
}

int main(int argc, char **argv) {
    init_bindings();

    if (argc == 1) {
        display_bindings(1);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "-l") == 0) {
            list_functions();
            return 0;
        }
        if (strcmp(arg, "-p") == 0) {
            display_bindings(1);
            return 0;
        }
        if (strcmp(arg, "-P") == 0) {
            display_bindings(0);
            return 0;
        }
        if (strcmp(arg, "-s") == 0) {
            display_macros(1);
            return 0;
        }
        if (strcmp(arg, "-S") == 0) {
            display_macros(0);
            return 0;
        }
        if (strcmp(arg, "-v") == 0) {
            list_variables(1);
            return 0;
        }
        if (strcmp(arg, "-V") == 0) {
            list_variables(0);
            return 0;
        }
        if (strcmp(arg, "-X") == 0) {
            display_shell_cmds();
            return 0;
        }
        if (strcmp(arg, "-m") == 0 && i + 1 < argc) {
            i++; /* ignore keymap */
            continue;
        }
        if (strcmp(arg, "-q") == 0 && i + 1 < argc) {
            query_function(argv[++i]);
            return 0;
        }
        if (strcmp(arg, "-u") == 0 && i + 1 < argc) {
            unbind_function(argv[++i]);
            return 0;
        }
        if (strcmp(arg, "-r") == 0 && i + 1 < argc) {
            remove_keyseq(argv[++i]);
            return 0;
        }
        if (strcmp(arg, "-x") == 0 && i + 1 < argc) {
            char *xarg = argv[++i];
            char *c = strchr(xarg, ':');
            if (c) {
                *c = '\0';
                add_binding(xarg, "", 0, c + 1);
            }
            return 0;
        }
        if (strcmp(arg, "-f") == 0 && i + 1 < argc) {
            const char *fname = argv[++i];
            FILE *f = fopen(fname, "r");
            if (!f) {
                fprintf(stderr, "bind: %s: cannot open file: %s\n", fname, strerror(errno));
                return 1;
            }
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) line[--l] = '\0';
                parse_binding_line(line);
            }
            fclose(f);
            return 0;
        }

        /* Non-option argument: binding line */
        parse_binding_line(arg);
    }

    return 0;
}
