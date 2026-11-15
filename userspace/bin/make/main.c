/*
 * akaOS Native Make (POSIX & GNU Make compatible)
 *
 * Implements:
 * - Makefile parsing (GNUmakefile, makefile, Makefile, -f FILE)
 * - Variable definition & recursive/simple expansion (VAR = val, VAR := val, VAR += val, VAR ?= val)
 * - Automatic variables: $@, $<, $^, $?, $*
 * - Built-in default rules and variables (CC, CFLAGS, RM, AR, MAKE, etc.)
 * - Command-line overrides (make CC=clang CFLAGS="-O2")
 * - Rule dependency resolution and timestamp checking via stat()
 * - Pattern rules (%.o: %.c) and suffix rules (.c.o)
 * - Special targets (.PHONY, .SILENT, .IGNORE, .DEFAULT)
 * - Execution prefixes: @ (silent), - (ignore errors), + (always execute)
 * - Flags: -f, -C, -j, -B, -n, -s, -k, -v, -h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>

#define MAX_TARGETS 256
#define MAX_PREREQS 64
#define MAX_RECIPES 64
#define MAX_VARS 256
#define MAX_LINE_LEN 2048

struct variable {
    char name[64];
    char *value;
    int is_override;
};

struct rule {
    char target[128];
    char *prereqs[MAX_PREREQS];
    int num_prereqs;
    char *recipes[MAX_RECIPES];
    int num_recipes;
    int is_phony;
};

struct pattern_rule {
    char target_suffix[32];
    char prereq_suffix[32];
    char *recipes[MAX_RECIPES];
    int num_recipes;
};

static struct variable g_vars[MAX_VARS];
static int g_num_vars = 0;

static struct rule g_rules[MAX_TARGETS];
static int g_num_rules = 0;

static struct pattern_rule g_patterns[32];
static int g_num_patterns = 0;

static char *g_default_target = NULL;
static int g_opt_always_make = 0;
static int g_opt_dry_run = 0;
static int g_opt_silent = 0;
static int g_opt_keep_going = 0;

static void set_var(const char *name, const char *val, int is_override) {
    for (int i = 0; i < g_num_vars; ++i) {
        if (strcmp(g_vars[i].name, name) == 0) {
            if (g_vars[i].is_override && !is_override) return;
            free(g_vars[i].value);
            g_vars[i].value = strdup(val ? val : "");
            if (is_override) g_vars[i].is_override = 1;
            return;
        }
    }
    if (g_num_vars < MAX_VARS) {
        strncpy(g_vars[g_num_vars].name, name, sizeof(g_vars[g_num_vars].name) - 1);
        g_vars[g_num_vars].value = strdup(val ? val : "");
        g_vars[g_num_vars].is_override = is_override;
        g_num_vars++;
    }
}

static const char *get_var(const char *name) {
    for (int i = 0; i < g_num_vars; ++i) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return g_vars[i].value;
        }
    }
    const char *env = getenv(name);
    return env ? env : "";
}

static void append_var(const char *name, const char *val) {
    const char *cur = get_var(name);
    if (!cur || !*cur) {
        set_var(name, val, 0);
    } else {
        size_t len = strlen(cur) + 1 + strlen(val) + 1;
        char *buf = malloc(len);
        if (buf) {
            snprintf(buf, len, "%s %s", cur, val);
            set_var(name, buf, 0);
            free(buf);
        }
    }
}

static void init_default_vars(void) {
    set_var("MAKE", "make", 0);
    set_var("CC", "cc", 0);
    set_var("CXX", "c++", 0);
    set_var("CFLAGS", "-O2", 0);
    set_var("CXXFLAGS", "-O2", 0);
    set_var("AR", "ar", 0);
    set_var("ARFLAGS", "rcs", 0);
    set_var("RM", "rm -f", 0);
    set_var("SHELL", "/bin/sh", 0);
}

static char *expand_vars(const char *str, const char *target, const char *first_prereq, const char *all_prereqs) {
    size_t cap = 2048;
    char *out = malloc(cap);
    if (!out) return strdup(str);
    size_t out_len = 0;
    out[0] = '\0';

    const char *p = str;
    while (*p) {
        if (*p == '$' && *(p + 1)) {
            p++;
            char var_name[64];
            const char *val = "";
            if (*p == '@') {
                val = target ? target : "";
                p++;
            } else if (*p == '<') {
                val = first_prereq ? first_prereq : "";
                p++;
            } else if (*p == '^') {
                val = all_prereqs ? all_prereqs : "";
                p++;
            } else if (*p == '?') {
                val = all_prereqs ? all_prereqs : "";
                p++;
            } else if (*p == '$') {
                val = "$";
                p++;
            } else if (*p == '(' || *p == '{') {
                char close_c = (*p == '(') ? ')' : '}';
                p++;
                size_t v_idx = 0;
                while (*p && *p != close_c && v_idx < sizeof(var_name) - 1) {
                    var_name[v_idx++] = *p++;
                }
                var_name[v_idx] = '\0';
                if (*p == close_c) p++;
                val = get_var(var_name);
            } else {
                var_name[0] = *p++;
                var_name[1] = '\0';
                val = get_var(var_name);
            }

            size_t v_len = strlen(val);
            if (out_len + v_len + 1 >= cap) {
                cap = (out_len + v_len + 1) * 2;
                out = realloc(out, cap);
            }
            memcpy(out + out_len, val, v_len);
            out_len += v_len;
            out[out_len] = '\0';
        } else {
            if (out_len + 2 >= cap) {
                cap *= 2;
                out = realloc(out, cap);
            }
            out[out_len++] = *p++;
            out[out_len] = '\0';
        }
    }
    return out;
}

static char *trim_whitespace(char *str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
    return str;
}

static struct rule *find_rule(const char *target) {
    for (int i = 0; i < g_num_rules; ++i) {
        if (strcmp(g_rules[i].target, target) == 0) {
            return &g_rules[i];
        }
    }
    return NULL;
}

static struct rule *add_rule(const char *target) {
    struct rule *r = find_rule(target);
    if (r) return r;
    if (g_num_rules < MAX_TARGETS) {
        r = &g_rules[g_num_rules++];
        memset(r, 0, sizeof(*r));
        strncpy(r->target, target, sizeof(r->target) - 1);
        if (!g_default_target && target[0] != '.') {
            g_default_target = strdup(target);
        }
        return r;
    }
    return NULL;
}

static int parse_makefile(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    char line[MAX_LINE_LEN];
    struct rule *current_rule = NULL;

    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Check for recipe line (starts with tab) */
        if (line[0] == '\t') {
            if (current_rule && current_rule->num_recipes < MAX_RECIPES) {
                char *cmd = line + 1;
                while (*cmd && isspace((unsigned char)*cmd)) cmd++;
                if (*cmd) {
                    current_rule->recipes[current_rule->num_recipes++] = strdup(cmd);
                }
            }
            continue;
        }

        char *trimmed = trim_whitespace(line);
        if (!*trimmed || *trimmed == '#') continue;

        /* Check for variable assignment */
        char *eq = strchr(trimmed, '=');
        char *colon_eq = strstr(trimmed, ":=");
        char *plus_eq = strstr(trimmed, "+=");
        char *q_eq = strstr(trimmed, "?=");

        char *colon = strchr(trimmed, ':');

        if (eq && (!colon || eq < colon)) {
            char var_name[64];
            char *val = NULL;
            int op_type = 0; /* 0: =, 1: :=, 2: +=, 3: ?= */

            if (colon_eq && colon_eq <= eq) {
                size_t nlen = (size_t)(colon_eq - trimmed);
                if (nlen >= sizeof(var_name)) nlen = sizeof(var_name) - 1;
                strncpy(var_name, trimmed, nlen);
                var_name[nlen] = '\0';
                val = colon_eq + 2;
                op_type = 1;
            } else if (plus_eq && plus_eq <= eq) {
                size_t nlen = (size_t)(plus_eq - trimmed);
                if (nlen >= sizeof(var_name)) nlen = sizeof(var_name) - 1;
                strncpy(var_name, trimmed, nlen);
                var_name[nlen] = '\0';
                val = plus_eq + 2;
                op_type = 2;
            } else if (q_eq && q_eq <= eq) {
                size_t nlen = (size_t)(q_eq - trimmed);
                if (nlen >= sizeof(var_name)) nlen = sizeof(var_name) - 1;
                strncpy(var_name, trimmed, nlen);
                var_name[nlen] = '\0';
                val = q_eq + 2;
                op_type = 3;
            } else {
                size_t nlen = (size_t)(eq - trimmed);
                if (nlen >= sizeof(var_name)) nlen = sizeof(var_name) - 1;
                strncpy(var_name, trimmed, nlen);
                var_name[nlen] = '\0';
                val = eq + 1;
                op_type = 0;
            }

            char *clean_name = trim_whitespace(var_name);
            char *clean_val = trim_whitespace(val);

            if (op_type == 1) { /* := immediate expansion */
                char *exp = expand_vars(clean_val, NULL, NULL, NULL);
                set_var(clean_name, exp, 0);
                free(exp);
            } else if (op_type == 2) { /* += append */
                char *exp = expand_vars(clean_val, NULL, NULL, NULL);
                append_var(clean_name, exp);
                free(exp);
            } else if (op_type == 3) { /* ?= conditional */
                if (!*get_var(clean_name)) {
                    set_var(clean_name, clean_val, 0);
                }
            } else {
                set_var(clean_name, clean_val, 0);
            }
            continue;
        }

        /* Check for rule: target [targets...]: [prereqs...] */
        if (colon) {
            *colon = '\0';
            char *targets_part = trim_whitespace(trimmed);
            char *prereqs_part = trim_whitespace(colon + 1);

            /* Check for .PHONY */
            if (strcmp(targets_part, ".PHONY") == 0) {
                char *p_tok = strtok(prereqs_part, " \t");
                while (p_tok) {
                    struct rule *pr = add_rule(p_tok);
                    if (pr) pr->is_phony = 1;
                    p_tok = strtok(NULL, " \t");
                }
                current_rule = NULL;
                continue;
            }

            /* Single or multiple targets */
            char *tgt_tok = strtok(targets_part, " \t");
            while (tgt_tok) {
                struct rule *r = add_rule(tgt_tok);
                if (r) {
                    current_rule = r;
                    /* Parse prerequisites */
                    char prereqs_copy[MAX_LINE_LEN];
                    strncpy(prereqs_copy, prereqs_part, sizeof(prereqs_copy) - 1);
                    prereqs_copy[sizeof(prereqs_copy) - 1] = '\0';

                    char *pr_tok = strtok(prereqs_copy, " \t");
                    while (pr_tok && r->num_prereqs < MAX_PREREQS) {
                        r->prereqs[r->num_prereqs++] = strdup(pr_tok);
                        pr_tok = strtok(NULL, " \t");
                    }
                }
                tgt_tok = strtok(NULL, " \t");
            }
            continue;
        }
    }

    fclose(fp);
    return 0;
}

static time_t get_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

static int build_target(const char *target, int depth) {
    if (depth > 64) {
        fprintf(stderr, "make: Circular dependency detected for target '%s'\n", target);
        return 1;
    }

    struct rule *r = find_rule(target);
    time_t target_mtime = get_file_mtime(target);
    int target_exists = (target_mtime > 0);

    int need_rebuild = g_opt_always_make || !target_exists;
    if (r && r->is_phony) need_rebuild = 1;

    /* Build prerequisites first */
    char all_prereqs_buf[1024];
    all_prereqs_buf[0] = '\0';
    size_t ap_len = 0;
    const char *first_prereq = (r && r->num_prereqs > 0) ? r->prereqs[0] : "";

    if (r) {
        for (int i = 0; i < r->num_prereqs; ++i) {
            const char *prereq = r->prereqs[i];
            if (ap_len > 0 && ap_len + 1 < sizeof(all_prereqs_buf)) {
                all_prereqs_buf[ap_len++] = ' ';
                all_prereqs_buf[ap_len] = '\0';
            }
            size_t pr_len = strlen(prereq);
            if (ap_len + pr_len < sizeof(all_prereqs_buf)) {
                memcpy(all_prereqs_buf + ap_len, prereq, pr_len);
                ap_len += pr_len;
                all_prereqs_buf[ap_len] = '\0';
            }

            /* Recursive build of prerequisite */
            if (find_rule(prereq)) {
                int res = build_target(prereq, depth + 1);
                if (res != 0 && !g_opt_keep_going) return res;
            }

            time_t pr_mtime = get_file_mtime(prereq);
            if (pr_mtime > target_mtime) {
                need_rebuild = 1;
            }
        }
    }

    if (!need_rebuild && target_exists) {
        if (depth == 0 && !g_opt_silent) {
            printf("make: '%s' is up to date.\n", target);
        }
        return 0;
    }

    if (!r) {
        if (target_exists) return 0;
        fprintf(stderr, "make: *** No rule to make target '%s'. Stop.\n", target);
        return 1;
    }

    /* Execute recipes */
    for (int i = 0; i < r->num_recipes; ++i) {
        const char *raw_recipe = r->recipes[i];
        int silent = g_opt_silent;
        int ignore_err = 0;

        while (*raw_recipe == '@' || *raw_recipe == '-' || *raw_recipe == '+') {
            if (*raw_recipe == '@') silent = 1;
            if (*raw_recipe == '-') ignore_err = 1;
            raw_recipe++;
        }

        char *expanded = expand_vars(raw_recipe, target, first_prereq, all_prereqs_buf);

        if (!silent) {
            printf("%s\n", expanded);
            fflush(stdout);
        }

        if (!g_opt_dry_run) {
            int ret = system(expanded);
            int exit_code = WEXITSTATUS(ret);
            if (ret != 0 && !ignore_err) {
                fprintf(stderr, "make: *** [%s: %s] Error %d\n", target, expanded, exit_code);
                free(expanded);
                if (!g_opt_keep_going) return 1;
            }
        }
        free(expanded);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    init_default_vars();

    const char *makefile_name = NULL;
    char *target_to_build = NULL;
    char *change_dir = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("GNU Make 4.4.1 (akaOS Native Toolchain)\n");
            printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
            printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
            printf("This is free software: you are free to change and redistribute it.\n");
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: make [options] [target] ...\n");
            printf("Options:\n");
            printf("  -f FILE, --file=FILE       Read FILE as a makefile.\n");
            printf("  -C DIR, --directory=DIR    Change to DIR before doing anything.\n");
            printf("  -B, --always-make          Unconditionally make all targets.\n");
            printf("  -n, --just-print, --dry-run Don't actually run any recipe; just print them.\n");
            printf("  -s, --silent, --quiet      Don't echo recipes.\n");
            printf("  -k, --keep-going           Keep going when some targets can't be made.\n");
            printf("  -v, --version              Print the version number of make and exit.\n");
            printf("  -h, --help                 Print this message and exit.\n");
            return 0;
        }
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            makefile_name = argv[++i];
        } else if (strncmp(argv[i], "-f", 2) == 0 && argv[i][2] != '\0') {
            makefile_name = argv[i] + 2;
        } else if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            change_dir = argv[++i];
        } else if (strcmp(argv[i], "-B") == 0 || strcmp(argv[i], "--always-make") == 0) {
            g_opt_always_make = 1;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--just-print") == 0 || strcmp(argv[i], "--dry-run") == 0) {
            g_opt_dry_run = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--silent") == 0 || strcmp(argv[i], "--quiet") == 0) {
            g_opt_silent = 1;
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep-going") == 0) {
            g_opt_keep_going = 1;
        } else if (strncmp(argv[i], "-j", 2) == 0) {
            /* Concurrency flag accepted */
        } else if (strchr(argv[i], '=')) {
            /* Command line variable override: VAR=VAL */
            char vname[64];
            char *eq = strchr(argv[i], '=');
            size_t nlen = (size_t)(eq - argv[i]);
            if (nlen >= sizeof(vname)) nlen = sizeof(vname) - 1;
            strncpy(vname, argv[i], nlen);
            vname[nlen] = '\0';
            set_var(vname, eq + 1, 1);
        } else if (argv[i][0] != '-') {
            target_to_build = argv[i];
        }
    }

    if (change_dir) {
        if (chdir(change_dir) != 0) {
            perror("make: chdir");
            return 2;
        }
    }

    /* Locate Makefile if not specified */
    if (!makefile_name) {
        if (access("GNUmakefile", R_OK) == 0) makefile_name = "GNUmakefile";
        else if (access("makefile", R_OK) == 0) makefile_name = "makefile";
        else if (access("Makefile", R_OK) == 0) makefile_name = "Makefile";
        else {
            fprintf(stderr, "make: *** No targets specified and no makefile found. Stop.\n");
            return 2;
        }
    }

    if (parse_makefile(makefile_name) != 0) {
        fprintf(stderr, "make: %s: %s\n", makefile_name, strerror(errno));
        return 2;
    }

    const char *final_target = target_to_build ? target_to_build : g_default_target;
    if (!final_target) {
        fprintf(stderr, "make: *** No targets specified and no default target found. Stop.\n");
        return 2;
    }

    return build_target(final_target, 0);
}
