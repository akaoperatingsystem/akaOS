#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_VARS 128
#define MAX_FUNCS 64
#define MAX_PARAMS 8
#define MAX_LINE 4096

struct bc_var {
    char name[32];
    double val;
};

static struct bc_var vars[MAX_VARS];
static int num_vars = 0;

static int opt_mathlib = 0;
static int opt_quiet = 0;
static int current_scale = 0;
static double last_val = 0.0;
static int should_quit = 0;

/* Functions */
struct bc_func {
    char name[32];
    char param[32];
    char body[1024];
};

static struct bc_func funcs[MAX_FUNCS];
static int num_funcs = 0;

static void print_usage(void) {
    printf("Usage: bc [options] [file ...]\n"
           "Options:\n"
           "  -h, --help         print usage and exit\n"
           "  -i, --interactive  force interactive mode\n"
           "  -l, --mathlib      define the standard math library\n"
           "  -w, --warn         give warnings for extensions to POSIX bc\n"
           "  -s, --standard     process exactly the POSIX bc language\n"
           "  -q, --quiet        do not print the normal GNU bc welcome\n"
           "  -v, --version      print version number and quit\n");
}

static void print_version(void) {
    printf("bc 1.07.1 (akaOS native implementation)\n"
           "Copyright 1991-2026 Free Software Foundation, Inc.\n");
}

static double get_var_val(const char *name) {
    if (strcmp(name, "scale") == 0) return (double)current_scale;
    if (strcmp(name, "last") == 0) return last_val;
    for (int i = 0; i < num_vars; ++i) {
        if (strcmp(vars[i].name, name) == 0) return vars[i].val;
    }
    return 0.0;
}

static void set_var_val(const char *name, double val) {
    if (strcmp(name, "scale") == 0) {
        current_scale = (int)val;
        if (current_scale < 0) current_scale = 0;
        return;
    }
    for (int i = 0; i < num_vars; ++i) {
        if (strcmp(vars[i].name, name) == 0) {
            vars[i].val = val;
            return;
        }
    }
    if (num_vars < MAX_VARS) {
        strncpy(vars[num_vars].name, name, sizeof(vars[num_vars].name) - 1);
        vars[num_vars].name[sizeof(vars[num_vars].name) - 1] = '\0';
        vars[num_vars].val = val;
        num_vars++;
    }
}

/* Print a number formatted with current_scale */
static void print_scaled_num(double val) {
    if (current_scale == 0) {
        if (fabs(val - (double)(long long)val) < 1e-9) {
            printf("%lld\n", (long long)val);
            return;
        }
    }
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%%.%df\n", current_scale);
    printf(fmt, val);
}

/* Recursive-descent expression parser */
static const char *parse_ptr = NULL;
static double eval_expr(void);

static void skip_ws(void) {
    while (parse_ptr && (*parse_ptr == ' ' || *parse_ptr == '\t' || *parse_ptr == '\r')) parse_ptr++;
}

static double eval_factor(void) {
    skip_ws();
    if (!parse_ptr || !*parse_ptr) return 0.0;

    if (*parse_ptr == '(') {
        parse_ptr++;
        double v = eval_expr();
        skip_ws();
        if (*parse_ptr == ')') parse_ptr++;
        return v;
    }

    if (*parse_ptr == '-') {
        parse_ptr++;
        return -eval_factor();
    }
    if (*parse_ptr == '+') {
        parse_ptr++;
        return eval_factor();
    }

    /* ++var */
    if (*parse_ptr == '+' && *(parse_ptr + 1) == '+') {
        parse_ptr += 2;
        skip_ws();
        char vname[32];
        size_t vn = 0;
        while (isalnum((unsigned char)*parse_ptr) || *parse_ptr == '_') {
            if (vn < sizeof(vname) - 1) vname[vn++] = *parse_ptr;
            parse_ptr++;
        }
        vname[vn] = '\0';
        double v = get_var_val(vname) + 1.0;
        set_var_val(vname, v);
        return v;
    }

    /* --var */
    if (*parse_ptr == '-' && *(parse_ptr + 1) == '-') {
        parse_ptr += 2;
        skip_ws();
        char vname[32];
        size_t vn = 0;
        while (isalnum((unsigned char)*parse_ptr) || *parse_ptr == '_') {
            if (vn < sizeof(vname) - 1) vname[vn++] = *parse_ptr;
            parse_ptr++;
        }
        vname[vn] = '\0';
        double v = get_var_val(vname) - 1.0;
        set_var_val(vname, v);
        return v;
    }

    /* Number literal */
    if (isdigit((unsigned char)*parse_ptr) || *parse_ptr == '.') {
        char *end;
        double v = strtod(parse_ptr, &end);
        parse_ptr = end;
        return v;
    }

    /* Identifier / Function call / Variable */
    if (isalpha((unsigned char)*parse_ptr) || *parse_ptr == '_') {
        char id[32];
        size_t id_len = 0;
        while (isalnum((unsigned char)*parse_ptr) || *parse_ptr == '_') {
            if (id_len < sizeof(id) - 1) id[id_len++] = *parse_ptr;
            parse_ptr++;
        }
        id[id_len] = '\0';

        skip_ws();
        /* Function call: id(expr) */
        if (*parse_ptr == '(') {
            parse_ptr++;
            double arg = eval_expr();
            skip_ws();
            if (*parse_ptr == ')') parse_ptr++;

            if (strcmp(id, "sqrt") == 0) {
                return (arg >= 0) ? sqrt(arg) : 0.0;
            }
            if (strcmp(id, "length") == 0) {
                char sbuf[64];
                snprintf(sbuf, sizeof(sbuf), "%.6g", arg);
                char *dot = strchr(sbuf, '.');
                size_t l = strlen(sbuf);
                if (dot) l--;
                if (sbuf[0] == '-') l--;
                return (double)l;
            }
            if (strcmp(id, "scale") == 0) {
                char sbuf[64];
                snprintf(sbuf, sizeof(sbuf), "%.12g", arg);
                char *dot = strchr(sbuf, '.');
                if (!dot) return 0.0;
                return (double)strlen(dot + 1);
            }
            if (strcmp(id, "s") == 0 && opt_mathlib) {
                return sin(arg);
            }
            if (strcmp(id, "c") == 0 && opt_mathlib) {
                return cos(arg);
            }
            if (strcmp(id, "l") == 0 && opt_mathlib) {
                return (arg > 0) ? log(arg) : 0.0;
            }
            if (strcmp(id, "e") == 0 && opt_mathlib) {
                return exp(arg);
            }

            /* Custom user-defined function */
            for (int f = 0; f < num_funcs; ++f) {
                if (strcmp(funcs[f].name, id) == 0) {
                    double saved_p = get_var_val(funcs[f].param);
                    set_var_val(funcs[f].param, arg);

                    /* Evaluate body: e.g. return(sqrt(x) + 10) or expr */
                    const char *b = funcs[f].body;
                    const char *ret_kw = strstr(b, "return");
                    double f_res = 0.0;
                    if (ret_kw) {
                        const char *saved_pp = parse_ptr;
                        parse_ptr = ret_kw + 6;
                        skip_ws();
                        f_res = eval_expr();
                        parse_ptr = saved_pp;
                    } else {
                        const char *saved_pp = parse_ptr;
                        parse_ptr = b;
                        f_res = eval_expr();
                        parse_ptr = saved_pp;
                    }

                    set_var_val(funcs[f].param, saved_p);
                    return f_res;
                }
            }

            return 0.0;
        }

        /* Postfix: var++ or var-- */
        if (*parse_ptr == '+' && *(parse_ptr + 1) == '+') {
            parse_ptr += 2;
            double old = get_var_val(id);
            set_var_val(id, old + 1.0);
            return old;
        }
        if (*parse_ptr == '-' && *(parse_ptr + 1) == '-') {
            parse_ptr += 2;
            double old = get_var_val(id);
            set_var_val(id, old - 1.0);
            return old;
        }

        /* Assignment: var = expr */
        if (*parse_ptr == '=' && *(parse_ptr + 1) != '=') {
            parse_ptr++;
            double res = eval_expr();
            set_var_val(id, res);
            return res;
        }

        return get_var_val(id);
    }

    return 0.0;
}

static double eval_power(void) {
    double v = eval_factor();
    for (;;) {
        skip_ws();
        if (!parse_ptr) break;
        if (*parse_ptr == '^') {
            parse_ptr++;
            double p = eval_factor();
            v = pow(v, p);
        } else {
            break;
        }
    }
    return v;
}

static double eval_term(void) {
    double v = eval_power();
    for (;;) {
        skip_ws();
        if (!parse_ptr) break;
        if (*parse_ptr == '*') {
            parse_ptr++;
            v *= eval_power();
        } else if (*parse_ptr == '/') {
            parse_ptr++;
            double d = eval_power();
            if (d != 0.0) v /= d;
        } else if (*parse_ptr == '%') {
            parse_ptr++;
            double d = eval_power();
            if (d != 0.0) v = (double)((long long)v % (long long)d);
        } else {
            break;
        }
    }
    return v;
}

static double eval_arith(void) {
    double v = eval_term();
    for (;;) {
        skip_ws();
        if (!parse_ptr) break;
        if (*parse_ptr == '+') {
            parse_ptr++;
            v += eval_term();
        } else if (*parse_ptr == '-') {
            parse_ptr++;
            v -= eval_term();
        } else {
            break;
        }
    }
    return v;
}

static double eval_expr(void) {
    double v = eval_arith();
    for (;;) {
        skip_ws();
        if (!parse_ptr) break;
        if (*parse_ptr == '=' && *(parse_ptr + 1) == '=') {
            parse_ptr += 2;
            v = (v == eval_arith());
        } else if (*parse_ptr == '!' && *(parse_ptr + 1) == '=') {
            parse_ptr += 2;
            v = (v != eval_arith());
        } else if (*parse_ptr == '<' && *(parse_ptr + 1) == '=') {
            parse_ptr += 2;
            v = (v <= eval_arith());
        } else if (*parse_ptr == '>' && *(parse_ptr + 1) == '=') {
            parse_ptr += 2;
            v = (v >= eval_arith());
        } else if (*parse_ptr == '<') {
            parse_ptr++;
            v = (v < eval_arith());
        } else if (*parse_ptr == '>') {
            parse_ptr++;
            v = (v > eval_arith());
        } else if (*parse_ptr == '&' && *(parse_ptr + 1) == '&') {
            parse_ptr += 2;
            double r = eval_arith();
            v = (v != 0.0 && r != 0.0);
        } else if (*parse_ptr == '|' && *(parse_ptr + 1) == '|') {
            parse_ptr += 2;
            double r = eval_arith();
            v = (v != 0.0 || r != 0.0);
        } else {
            break;
        }
    }
    return v;
}

/* Parse and execute a single statement/line */
static void execute_statement(const char *stmt) {
    while (*stmt == ' ' || *stmt == '\t' || *stmt == '\r') stmt++;
    if (!*stmt || *stmt == '\n') return;

    if (strncmp(stmt, "quit", 4) == 0) {
        should_quit = 1;
        return;
    }

    /* Quoted string: "text" */
    if (*stmt == '"') {
        const char *p = stmt + 1;
        while (*p && *p != '"') {
            putchar(*p++);
        }
        return;
    }

    int is_assignment = 0;
    const char *eq = strchr(stmt, '=');
    if (eq && *(eq + 1) != '=' && *(eq - 1) != '!' && *(eq - 1) != '<' && *(eq - 1) != '>') {
        is_assignment = 1;
    }

    parse_ptr = stmt;
    double res = eval_expr();
    last_val = res;

    if (!is_assignment) {
        print_scaled_num(res);
    }
}

/* Process buffer / stream */
static void process_stream(FILE *f) {
    char line[MAX_LINE];
    char function_def_buf[MAX_LINE];
    size_t function_def_len = 0;
    int inside_func = 0;
    char cur_func_name[32] = "";
    char cur_func_param[32] = "";

    while (fgets(line, sizeof(line), f)) {
        if (should_quit) break;

        /* Strip inline comments */
        char clean[MAX_LINE];
        size_t cidx = 0;
        int in_comment = 0;
        for (size_t i = 0; line[i]; ++i) {
            if (line[i] == '#' && !in_comment) break;
            if (line[i] == '/' && line[i + 1] == '*') {
                in_comment = 1;
                i++;
                continue;
            }
            if (line[i] == '*' && line[i + 1] == '/' && in_comment) {
                in_comment = 0;
                i++;
                continue;
            }
            if (!in_comment) {
                clean[cidx++] = line[i];
            }
        }
        clean[cidx] = '\0';

        char *p = clean;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n') continue;

        /* Check define func(param) */
        if (strncmp(p, "define", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            inside_func = 1;
            function_def_buf[0] = '\0';
            function_def_len = 0;
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            char *open_p = strchr(p, '(');
            if (open_p) {
                size_t nl = (size_t)(open_p - p);
                if (nl >= sizeof(cur_func_name)) nl = sizeof(cur_func_name) - 1;
                memcpy(cur_func_name, p, nl);
                cur_func_name[nl] = '\0';
                p = open_p + 1;
                char *close_p = strchr(p, ')');
                if (close_p) {
                    size_t pl = (size_t)(close_p - p);
                    if (pl >= sizeof(cur_func_param)) pl = sizeof(cur_func_param) - 1;
                    memcpy(cur_func_param, p, pl);
                    cur_func_param[pl] = '\0';
                }
            }
            continue;
        }

        if (inside_func) {
            size_t len = strlen(p);
            if (len >= sizeof(function_def_buf) - function_def_len) {
                fprintf(stderr, "bc: function definition too long\n");
                return;
            }
            memcpy(function_def_buf + function_def_len, p, len + 1);
            function_def_len += len;
            if (strchr(p, '}')) {
                inside_func = 0;
                if (num_funcs < MAX_FUNCS) {
                    strncpy(funcs[num_funcs].name, cur_func_name, sizeof(funcs[num_funcs].name) - 1);
                    funcs[num_funcs].name[sizeof(funcs[num_funcs].name) - 1] = '\0';
                    strncpy(funcs[num_funcs].param, cur_func_param, sizeof(funcs[num_funcs].param) - 1);
                    funcs[num_funcs].param[sizeof(funcs[num_funcs].param) - 1] = '\0';
                    strncpy(funcs[num_funcs].body, function_def_buf, sizeof(funcs[num_funcs].body) - 1);
                    funcs[num_funcs].body[sizeof(funcs[num_funcs].body) - 1] = '\0';
                    num_funcs++;
                }
            }
            continue;
        }

        /* Split semicolons or execute statements */
        char *tok = p;
        while (*tok) {
            while (*tok == ' ' || *tok == '\t' || *tok == ';') tok++;
            if (!*tok || *tok == '\n') break;

            char sub[MAX_LINE];
            size_t sidx = 0;
            if (*tok == '"') {
                sub[sidx++] = *tok++;
                while (*tok && *tok != '"') {
                    if (sidx < sizeof(sub) - 1) sub[sidx++] = *tok;
                    tok++;
                }
                if (*tok == '"') sub[sidx++] = *tok++;
            } else {
                while (*tok && *tok != ';' && *tok != '\n') {
                    if (sidx < sizeof(sub) - 1) sub[sidx++] = *tok;
                    tok++;
                }
            }
            sub[sidx] = '\0';
            execute_statement(sub);
            if (should_quit) break;
        }
    }
}

int main(int argc, char **argv) {
    int file_start = argc;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "--mathlib") == 0) {
            opt_mathlib = 1;
            current_scale = 20;
            continue;
        }
        if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            opt_quiet = 1;
            continue;
        }
        if (strcmp(arg, "-i") == 0 || strcmp(arg, "--interactive") == 0 ||
            strcmp(arg, "-w") == 0 || strcmp(arg, "--warn") == 0 ||
            strcmp(arg, "-s") == 0 || strcmp(arg, "--standard") == 0) {
            continue;
        }
        if (arg[0] == '-') {
            continue;
        }
        file_start = i;
        break;
    }

    if (!opt_quiet && isatty(STDIN_FILENO) && file_start >= argc) {
        printf("bc 1.07.1\n"
               "Copyright 1991-2026 Free Software Foundation, Inc.\n"
               "This is free software with ABSOLUTELY NO WARRANTY.\n"
               "For details type `warranty'.\n");
    }

    if (file_start < argc) {
        for (int i = file_start; i < argc; ++i) {
            const char *fname = argv[i];
            FILE *f = fopen(fname, "r");
            if (!f) {
                fprintf(stderr, "bc: cannot open '%s': %s\n", fname, strerror(errno));
                continue;
            }
            process_stream(f);
            fclose(f);
            if (should_quit) return 0;
        }
    } else {
        process_stream(stdin);
    }

    return 0;
}
