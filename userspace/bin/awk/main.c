#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FIELDS 128
#define MAX_LINE 4096
#define MAX_VARS 64
#define MAX_RULES 64
#define MAX_SOURCE 16384

/* Built-in and user variables */
struct awk_var {
    char name[32];
    char sval[256];
    double nval;
    int is_numeric;
};

static struct awk_var vars[MAX_VARS];
static int var_count = 0;

/* Special variables */
static char var_FS[32] = " ";
static char var_OFS[32] = " ";
static char var_RS[32] = "\n";
static char var_ORS[32] = "\n";
static long var_NR = 0;
static long var_FNR = 0;
static long var_NF = 0;
static char var_FILENAME[256] = "";

/* Rules */
enum pattern_type {
    PAT_ALWAYS,
    PAT_BEGIN,
    PAT_END,
    PAT_REGEX,
    PAT_EXPR
};

struct awk_rule {
    enum pattern_type pat_type;
    char pat_str[256];
    regex_t preg;
    int preg_valid;
    char action[1024];
};

static struct awk_rule rules[MAX_RULES];
static int rule_count = 0;

static void set_var(const char *name, const char *sval, double nval, int is_num) {
    if (strcmp(name, "FS") == 0) {
        strncpy(var_FS, sval, sizeof(var_FS) - 1);
        var_FS[sizeof(var_FS) - 1] = '\0';
    } else if (strcmp(name, "OFS") == 0) {
        strncpy(var_OFS, sval, sizeof(var_OFS) - 1);
        var_OFS[sizeof(var_OFS) - 1] = '\0';
    } else if (strcmp(name, "RS") == 0) {
        strncpy(var_RS, sval, sizeof(var_RS) - 1);
        var_RS[sizeof(var_RS) - 1] = '\0';
    } else if (strcmp(name, "ORS") == 0) {
        strncpy(var_ORS, sval, sizeof(var_ORS) - 1);
        var_ORS[sizeof(var_ORS) - 1] = '\0';
    } else if (strcmp(name, "NF") == 0) {
        var_NF = is_num ? (long)nval : atol(sval);
    } else if (strcmp(name, "NR") == 0) {
        var_NR = is_num ? (long)nval : atol(sval);
    } else if (strcmp(name, "FNR") == 0) {
        var_FNR = is_num ? (long)nval : atol(sval);
    }

    for (int i = 0; i < var_count; ++i) {
        if (strcmp(vars[i].name, name) == 0) {
            strncpy(vars[i].sval, sval, sizeof(vars[i].sval) - 1);
            vars[i].sval[sizeof(vars[i].sval) - 1] = '\0';
            vars[i].nval = nval;
            vars[i].is_numeric = is_num;
            return;
        }
    }
    if (var_count < MAX_VARS) {
        strncpy(vars[var_count].name, name, sizeof(vars[var_count].name) - 1);
        vars[var_count].name[sizeof(vars[var_count].name) - 1] = '\0';
        strncpy(vars[var_count].sval, sval, sizeof(vars[var_count].sval) - 1);
        vars[var_count].sval[sizeof(vars[var_count].sval) - 1] = '\0';
        vars[var_count].nval = nval;
        vars[var_count].is_numeric = is_num;
        var_count++;
    }
}

static struct awk_var *get_var(const char *name) {
    for (int i = 0; i < var_count; ++i) {
        if (strcmp(vars[i].name, name) == 0) return &vars[i];
    }
    return NULL;
}

/* Fields storage for current record */
static char raw_line[MAX_LINE];
static char fields[MAX_FIELDS][MAX_LINE];

static void split_fields(const char *line) {
    var_NF = 0;
    strncpy(raw_line, line, sizeof(raw_line) - 1);
    raw_line[sizeof(raw_line) - 1] = '\0';
    strncpy(fields[0], raw_line, sizeof(fields[0]) - 1);
    fields[0][sizeof(fields[0]) - 1] = '\0';

    const char *p = line;
    if (strcmp(var_FS, " ") == 0 || var_FS[0] == '\0') {
        while (*p && var_NF < MAX_FIELDS - 1) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
            size_t flen = (size_t)(p - start);
            var_NF++;
            if (flen >= MAX_LINE) flen = MAX_LINE - 1;
            memcpy(fields[var_NF], start, flen);
            fields[var_NF][flen] = '\0';
        }
    } else {
        char sep = var_FS[0];
        while (*p && var_NF < MAX_FIELDS - 1) {
            const char *start = p;
            while (*p && *p != sep && *p != '\r' && *p != '\n') p++;
            size_t flen = (size_t)(p - start);
            var_NF++;
            if (flen >= MAX_LINE) flen = MAX_LINE - 1;
            memcpy(fields[var_NF], start, flen);
            fields[var_NF][flen] = '\0';
            if (*p == sep) p++;
        }
    }
}

/* Evaluate an expression string like "$1", "NR", "length($0)", "5", "sum + $5" */
static double eval_numeric_expr(const char *expr, const char **str_res) {
    while (*expr == ' ' || *expr == '\t') expr++;
    if (!*expr) {
        if (str_res) *str_res = "";
        return 0.0;
    }

    /* Field reference: $0, $1, $NF, $(NF-2) */
    if (*expr == '$') {
        expr++;
        int f_idx = 0;
        if (*expr == '(') {
            /* e.g. $(NF-2) */
            char inner[64];
            const char *close = strchr(expr, ')');
            if (close) {
                size_t in_len = (size_t)(close - (expr + 1));
                if (in_len >= sizeof(inner)) in_len = sizeof(inner) - 1;
                memcpy(inner, expr + 1, in_len);
                inner[in_len] = '\0';
                f_idx = (int)eval_numeric_expr(inner, NULL);
            }
        } else if (isdigit((unsigned char)*expr)) {
            f_idx = atoi(expr);
        } else if (strncmp(expr, "NF", 2) == 0) {
            f_idx = (int)var_NF;
        }
        if (f_idx >= 0 && f_idx <= var_NF) {
            if (str_res) *str_res = fields[f_idx];
            return atof(fields[f_idx]);
        }
        if (str_res) *str_res = "";
        return 0.0;
    }

    /* Built-in variable names */
    if (strcmp(expr, "NR") == 0) {
        if (str_res) *str_res = NULL;
        return (double)var_NR;
    }
    if (strcmp(expr, "NF") == 0) {
        if (str_res) *str_res = NULL;
        return (double)var_NF;
    }
    if (strcmp(expr, "FNR") == 0) {
        if (str_res) *str_res = NULL;
        return (double)var_FNR;
    }

    /* Check length($0) or length(str) */
    if (strncmp(expr, "length(", 7) == 0) {
        const char *arg_start = expr + 7;
        const char *close = strchr(arg_start, ')');
        if (close) {
            char inner[64];
            size_t in_len = (size_t)(close - arg_start);
            if (in_len >= sizeof(inner)) in_len = sizeof(inner) - 1;
            memcpy(inner, arg_start, in_len);
            inner[in_len] = '\0';
            const char *s = "";
            eval_numeric_expr(inner, &s);
            if (str_res) *str_res = NULL;
            return (double)strlen(s);
        }
    }

    /* Check user variable */
    struct awk_var *v = get_var(expr);
    if (v) {
        if (str_res) *str_res = v->sval;
        return v->nval;
    }

    /* Literal number or string */
    if (*expr == '"') {
        /* Quoted string */
        static char sbuf[256];
        size_t slen = strlen(expr);
        if (slen >= 2 && expr[slen - 1] == '"') {
            size_t inner_len = slen - 2;
            if (inner_len >= sizeof(sbuf)) inner_len = sizeof(sbuf) - 1;
            memcpy(sbuf, expr + 1, inner_len);
            sbuf[inner_len] = '\0';
            if (str_res) *str_res = sbuf;
            return atof(sbuf);
        }
    }

    if (isdigit((unsigned char)*expr) || *expr == '-' || *expr == '+') {
        if (str_res) *str_res = expr;
        return atof(expr);
    }

    if (str_res) *str_res = expr;
    return 0.0;
}

/* Evaluate pattern condition */
static int eval_pattern(struct awk_rule *rule) {
    if (rule->pat_type == PAT_ALWAYS) return 1;
    if (rule->pat_type == PAT_BEGIN || rule->pat_type == PAT_END) return 0;
    if (rule->pat_type == PAT_REGEX) {
        if (rule->preg_valid) {
            return regexec(&rule->preg, raw_line, 0, NULL, 0) == 0;
        }
        return strstr(raw_line, rule->pat_str) != NULL;
    }

    const char *p = rule->pat_str;
    while (*p == ' ' || *p == '\t') p++;

    /* e.g. NF > 0 */
    if (strncmp(p, "NF > 0", 6) == 0 || strncmp(p, "NF>0", 4) == 0) {
        return var_NF > 0;
    }

    /* Comparison: LHS == RHS, LHS != RHS, LHS > RHS, LHS < RHS, LHS >= RHS, LHS <= RHS */
    const char *op = NULL;
    int op_type = 0; /* 1: ==, 2: !=, 3: >=, 4: <=, 5: >, 6: <, 7: % */
    if ((op = strstr(p, "=="))) op_type = 1;
    else if ((op = strstr(p, "!="))) op_type = 2;
    else if ((op = strstr(p, ">="))) op_type = 3;
    else if ((op = strstr(p, "<="))) op_type = 4;
    else if ((op = strstr(p, ">"))) op_type = 5;
    else if ((op = strstr(p, "<"))) op_type = 6;
    else if ((op = strstr(p, "%"))) op_type = 7;

    if (op && op_type > 0) {
        char left[128], right[128];
        size_t llen = (size_t)(op - p);
        if (llen >= sizeof(left)) llen = sizeof(left) - 1;
        memcpy(left, p, llen);
        left[llen] = '\0';

        size_t oplen = (op_type == 1 || op_type == 2 || op_type == 3 || op_type == 4) ? 2 : 1;
        strncpy(right, op + oplen, sizeof(right) - 1);
        right[sizeof(right) - 1] = '\0';

        /* Trim */
        char *l = left; while (*l == ' ') l++;
        char *r = right; while (*r == ' ') r++;
        size_t l_len = strlen(l); while (l_len > 0 && l[l_len - 1] == ' ') l[--l_len] = '\0';
        size_t r_len = strlen(r); while (r_len > 0 && r[r_len - 1] == ' ') r[--r_len] = '\0';

        const char *l_str = NULL;
        const char *r_str = NULL;
        double l_num = eval_numeric_expr(l, &l_str);
        double r_num = eval_numeric_expr(r, &r_str);

        if (op_type == 1) {
            /* == */
            if (l_str && r_str && (*l == '"' || *r == '"' || !isdigit((unsigned char)*l_str))) {
                return strcmp(l_str, r_str) == 0;
            }
            return l_num == r_num;
        }
        if (op_type == 2) {
            /* != */
            if (l_str && r_str && (*l == '"' || *r == '"' || !isdigit((unsigned char)*l_str))) {
                return strcmp(l_str, r_str) != 0;
            }
            return l_num != r_num;
        }
        if (op_type == 3) return l_num >= r_num;
        if (op_type == 4) return l_num <= r_num;
        if (op_type == 5) return l_num > r_num;
        if (op_type == 6) return l_num < r_num;
        if (op_type == 7) {
            /* e.g. NR % 2 == 0 handled above or directly */
            return ((long)l_num % (long)r_num) != 0;
        }
    }

    return 1;
}

/* Execute action inside { ... } */
static void execute_action(const char *action) {
    if (!action || !*action) {
        /* Default action: print $0 */
        printf("%s%s", raw_line, var_ORS);
        return;
    }

    const char *p = action;
    while (*p == ' ' || *p == '\t' || *p == '{') p++;

    /* Assignments: x += expr, sum += expr, var = expr */
    if (strstr(p, "+=")) {
        char vname[32];
        const char *plus = strstr(p, "+=");
        size_t vnlen = (size_t)(plus - p);
        while (vnlen > 0 && (p[vnlen - 1] == ' ' || p[vnlen - 1] == '\t')) vnlen--;
        if (vnlen < sizeof(vname)) {
            memcpy(vname, p, vnlen);
            vname[vnlen] = '\0';
            const char *val_expr = plus + 2;
            while (*val_expr == ' ' || *val_expr == '\t') val_expr++;
            double add_val = eval_numeric_expr(val_expr, NULL);
            struct awk_var *v = get_var(vname);
            double cur_val = v ? v->nval : 0.0;
            double new_val = cur_val + add_val;
            char sbuf[64];
            snprintf(sbuf, sizeof(sbuf), "%.6g", new_val);
            set_var(vname, sbuf, new_val, 1);
        }
    } else if (strstr(p, "++")) {
        char vname[32];
        const char *pp = strstr(p, "++");
        size_t vnlen = (size_t)(pp - p);
        while (vnlen > 0 && (p[vnlen - 1] == ' ' || p[vnlen - 1] == '\t')) vnlen--;
        if (vnlen < sizeof(vname)) {
            memcpy(vname, p, vnlen);
            vname[vnlen] = '\0';
            struct awk_var *v = get_var(vname);
            double cur_val = v ? v->nval : 0.0;
            double new_val = cur_val + 1.0;
            char sbuf[64];
            snprintf(sbuf, sizeof(sbuf), "%.6g", new_val);
            set_var(vname, sbuf, new_val, 1);
        }
    }

    /* Print statements: print item1, item2, "string", ... */
    const char *pr = strstr(p, "print");
    if (pr) {
        pr += 5;
        while (*pr == ' ' || *pr == '\t') pr++;
        if (!*pr || *pr == '}' || *pr == ';') {
            printf("%s%s", raw_line, var_ORS);
            return;
        }

        /* Parse comma-separated or space-separated items */
        char arg_str[512];
        strncpy(arg_str, pr, sizeof(arg_str) - 1);
        arg_str[sizeof(arg_str) - 1] = '\0';
        char *end_brace = strchr(arg_str, '}');
        if (end_brace) *end_brace = '\0';
        char *semi = strchr(arg_str, ';');
        if (semi) *semi = '\0';

        char *tok = arg_str;
        int first_item = 1;
        while (*tok) {
            while (*tok == ' ' || *tok == '\t') tok++;
            if (!*tok) break;

            char item[128];
            size_t ilen = 0;
            if (*tok == '"') {
                tok++;
                while (*tok && *tok != '"') {
                    if (ilen < sizeof(item) - 1) item[ilen++] = *tok;
                    tok++;
                }
                if (*tok == '"') tok++;
            } else {
                while (*tok && *tok != ',' && *tok != ' ' && *tok != '\t') {
                    if (ilen < sizeof(item) - 1) item[ilen++] = *tok;
                    tok++;
                }
            }
            item[ilen] = '\0';
            if (*tok == ',') {
                tok++;
            }

            const char *res_s = NULL;
            double res_n = eval_numeric_expr(item, &res_s);
            if (!first_item) {
                printf("%s", var_OFS);
            }
            if (res_s && *res_s) {
                printf("%s", res_s);
            } else {
                printf("%.6g", res_n);
            }
            first_item = 0;
        }
        printf("%s", var_ORS);
    }
}

/* Parse AWK program rules */
static int parse_program(const char *prog) {
    rule_count = 0;
    char buffer[MAX_SOURCE];
    strncpy(buffer, prog, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *cursor = buffer;
    while (*cursor && rule_count < MAX_RULES) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ';') cursor++;
        if (!*cursor) break;

        /* Comment */
        if (*cursor == '#') {
            while (*cursor && *cursor != '\n') cursor++;
            continue;
        }

        struct awk_rule *rule = &rules[rule_count];
        memset(rule, 0, sizeof(*rule));

        if (strncmp(cursor, "BEGIN", 5) == 0 && (cursor[5] == ' ' || cursor[5] == '\t' || cursor[5] == '{')) {
            rule->pat_type = PAT_BEGIN;
            cursor += 5;
        } else if (strncmp(cursor, "END", 3) == 0 && (cursor[3] == ' ' || cursor[3] == '\t' || cursor[3] == '{')) {
            rule->pat_type = PAT_END;
            cursor += 3;
        } else if (*cursor == '/') {
            rule->pat_type = PAT_REGEX;
            cursor++;
            char *pat_end = strchr(cursor, '/');
            if (pat_end) {
                size_t plen = (size_t)(pat_end - cursor);
                if (plen >= sizeof(rule->pat_str)) plen = sizeof(rule->pat_str) - 1;
                memcpy(rule->pat_str, cursor, plen);
                rule->pat_str[plen] = '\0';
                cursor = pat_end + 1;
                if (regcomp(&rule->preg, rule->pat_str, REG_EXTENDED) == 0) {
                    rule->preg_valid = 1;
                }
            }
        } else if (*cursor == '{') {
            rule->pat_type = PAT_ALWAYS;
        } else {
            /* Expression pattern before { */
            rule->pat_type = PAT_EXPR;
            char *brace = strchr(cursor, '{');
            if (brace) {
                size_t plen = (size_t)(brace - cursor);
                if (plen >= sizeof(rule->pat_str)) plen = sizeof(rule->pat_str) - 1;
                memcpy(rule->pat_str, cursor, plen);
                rule->pat_str[plen] = '\0';
                cursor = brace;
            } else {
                /* Pattern without action: print $0 */
                strncpy(rule->pat_str, cursor, sizeof(rule->pat_str) - 1);
                rule->pat_str[sizeof(rule->pat_str) - 1] = '\0';
                rule->action[0] = '\0';
                rule_count++;
                break;
            }
        }

        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (*cursor == '{') {
            cursor++;
            char *close_brace = strchr(cursor, '}');
            if (close_brace) {
                size_t alen = (size_t)(close_brace - cursor);
                if (alen >= sizeof(rule->action)) alen = sizeof(rule->action) - 1;
                memcpy(rule->action, cursor, alen);
                rule->action[alen] = '\0';
                cursor = close_brace + 1;
            } else {
                strncpy(rule->action, cursor, sizeof(rule->action) - 1);
                rule->action[sizeof(rule->action) - 1] = '\0';
                cursor += strlen(cursor);
            }
        }
        rule_count++;
    }
    return rule_count;
}

static void process_file(FILE *f, const char *filename) {
    strncpy(var_FILENAME, filename, sizeof(var_FILENAME) - 1);
    var_FILENAME[sizeof(var_FILENAME) - 1] = '\0';
    var_FNR = 0;

    char line_buf[MAX_LINE];
    while (fgets(line_buf, sizeof(line_buf), f)) {
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\r' || line_buf[len - 1] == '\n')) {
            line_buf[--len] = '\0';
        }
        var_NR++;
        var_FNR++;
        split_fields(line_buf);

        for (int r = 0; r < rule_count; ++r) {
            if (rules[r].pat_type == PAT_BEGIN || rules[r].pat_type == PAT_END) continue;
            if (eval_pattern(&rules[r])) {
                execute_action(rules[r].action);
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *program_text = NULL;
    const char *program_file = NULL;
    int files_start = argc;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printf("Usage: awk [options] 'program' [file ...]\n"
                   "       awk -f program-file [options] [file ...]\n");
            return 0;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
            printf("GNU Awk 5.3.0 (akaOS native implementation)\n");
            return 0;
        }
        if (strncmp(arg, "-F", 2) == 0 || strcmp(arg, "--field-separator") == 0) {
            const char *fs_val = NULL;
            if (strcmp(arg, "--field-separator") == 0 && i + 1 < argc) fs_val = argv[++i];
            else if (arg[2] != '\0') fs_val = arg + 2;
            else if (i + 1 < argc) fs_val = argv[++i];
            if (fs_val) {
                strncpy(var_FS, fs_val, sizeof(var_FS) - 1);
                var_FS[sizeof(var_FS) - 1] = '\0';
            }
            continue;
        }
        if (strncmp(arg, "-v", 2) == 0 || strcmp(arg, "--assign") == 0) {
            const char *assign_val = NULL;
            if (strcmp(arg, "--assign") == 0 && i + 1 < argc) assign_val = argv[++i];
            else if (arg[2] != '\0') assign_val = arg + 2;
            else if (i + 1 < argc) assign_val = argv[++i];
            if (assign_val) {
                char *eq = strchr(assign_val, '=');
                if (eq) {
                    char vname[32];
                    size_t vlen = (size_t)(eq - assign_val);
                    if (vlen >= sizeof(vname)) vlen = sizeof(vname) - 1;
                    memcpy(vname, assign_val, vlen);
                    vname[vlen] = '\0';
                    const char *val = eq + 1;
                    set_var(vname, val, atof(val), 1);
                }
            }
            continue;
        }
        if (strncmp(arg, "-f", 2) == 0 || strcmp(arg, "--file") == 0) {
            if (strcmp(arg, "--file") == 0 && i + 1 < argc) program_file = argv[++i];
            else if (arg[2] != '\0') program_file = arg + 2;
            else if (i + 1 < argc) program_file = argv[++i];
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            if (!program_text && !program_file && i + 1 < argc) {
                program_text = argv[++i];
            }
            files_start = i + 1;
            break;
        }
        if (arg[0] == '-') {
            /* Ignore compatibility / lint flags */
            continue;
        }
        if (!program_text && !program_file) {
            program_text = arg;
            files_start = i + 1;
            break;
        }
    }

    char file_source[MAX_SOURCE];
    if (program_file) {
        int fd = open(program_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "awk: cannot open program file '%s': %s\n", program_file, strerror(errno));
            return 1;
        }
        ssize_t n = read(fd, file_source, sizeof(file_source) - 1);
        close(fd);
        if (n >= 0) file_source[n] = '\0'; else file_source[0] = '\0';
        program_text = file_source;
    }

    if (!program_text) {
        fprintf(stderr, "awk: no program text specified\n");
        return 1;
    }

    parse_program(program_text);

    /* Execute BEGIN rules */
    for (int r = 0; r < rule_count; ++r) {
        if (rules[r].pat_type == PAT_BEGIN) {
            execute_action(rules[r].action);
        }
    }

    /* Process input files or stdin */
    if (files_start >= argc) {
        process_file(stdin, "-");
    } else {
        for (int i = files_start; i < argc; ++i) {
            const char *fname = argv[i];
            FILE *f = (strcmp(fname, "-") == 0) ? stdin : fopen(fname, "r");
            if (!f) {
                fprintf(stderr, "awk: cannot open file '%s': %s\n", fname, strerror(errno));
                continue;
            }
            process_file(f, fname);
            if (f != stdin) fclose(f);
        }
    }

    /* Execute END rules */
    for (int r = 0; r < rule_count; ++r) {
        if (rules[r].pat_type == PAT_END) {
            execute_action(rules[r].action);
        }
    }

    for (int r = 0; r < rule_count; ++r) {
        if (rules[r].preg_valid) regfree(&rules[r].preg);
    }

    return 0;
}
