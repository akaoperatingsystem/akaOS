#include <akaos/fs.h>
#include <akaos/architecture.h>
#include <akaos/account.h>
#include <akaos/version.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <unistd.h>

#define MAX_STAGES 8
#define MAX_ARGS 64
#define GLOB_PATH_MAX 128
#define MAX_SHELL_VARS 64

struct shell_job {
    int id;
    int pid;
    int state; /* 0 = unused, 1 = running, 2 = stopped */
    int pgid;
    int process_count;
    int processes[MAX_STAGES];
    char cmd[48];
};

#define MAX_SHELL_JOBS 32
static struct shell_job shell_jobs[MAX_SHELL_JOBS];
static int next_job_id = 1;

static int last_exit_status = 0;
static struct akaos_account shell_account;
static struct shell_job *find_job_by_id(int id);

static int wait_status_exit_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

/* Script positional parameters */
static char script_arg0[128] = "akash";
static char script_args[10][128];
static int script_argc = 0;

/* The interactive shell catches terminal interrupts so Ctrl+C clears the
 * current read and returns a fresh prompt.  Child jobs reset this disposition
 * on exec, allowing foreground programs to receive the default SIGINT. */
static void shell_sigint_handler(int signal_number) {
    (void)signal_number;
}

/* Shell custom variables table */
struct shell_var {
    char name[32];
    char val[128];
};
static struct shell_var shell_vars[MAX_SHELL_VARS];
static int num_shell_vars = 0;

static const char *get_shell_var(const char *name) {
    for (int i = 0; i < num_shell_vars; ++i) {
        if (strcmp(shell_vars[i].name, name) == 0) {
            return shell_vars[i].val;
        }
    }
    return NULL;
}

static void set_shell_var(const char *name, const char *val) {
    for (int i = 0; i < num_shell_vars; ++i) {
        if (strcmp(shell_vars[i].name, name) == 0) {
            strncpy(shell_vars[i].val, val, sizeof(shell_vars[i].val) - 1);
            shell_vars[i].val[sizeof(shell_vars[i].val) - 1] = '\0';
            return;
        }
    }
    if (num_shell_vars < MAX_SHELL_VARS) {
        strncpy(shell_vars[num_shell_vars].name, name, sizeof(shell_vars[num_shell_vars].name) - 1);
        shell_vars[num_shell_vars].name[sizeof(shell_vars[num_shell_vars].name) - 1] = '\0';
        strncpy(shell_vars[num_shell_vars].val, val, sizeof(shell_vars[num_shell_vars].val) - 1);
        shell_vars[num_shell_vars].val[sizeof(shell_vars[num_shell_vars].val) - 1] = '\0';
        num_shell_vars++;
    }
}

static int add_shell_job(int pid, int state, const char *cmd) {
    for (int i = 0; i < MAX_SHELL_JOBS; ++i) {
        if (shell_jobs[i].state == 0) {
            shell_jobs[i].id = next_job_id++;
            shell_jobs[i].pid = pid;
            shell_jobs[i].state = state;
            shell_jobs[i].pgid = pid;
            shell_jobs[i].process_count = 1;
            shell_jobs[i].processes[0] = pid;
            size_t c = 0;
            while (cmd && cmd[c] && c < sizeof(shell_jobs[i].cmd) - 1) {
                shell_jobs[i].cmd[c] = cmd[c];
                ++c;
            }
            shell_jobs[i].cmd[c] = '\0';
            return shell_jobs[i].id;
        }
    }
    return 1;
}

static int add_pipeline_job(const int *pids, int count, int pgid, int state,
                            const char *cmd) {
    int id = add_shell_job(pgid, state, cmd);
    struct shell_job *job = find_job_by_id(id);
    if (!job) return id;
    job->pgid = pgid;
    job->process_count = 0;
    for (int index = 0; index < count && job->process_count < MAX_STAGES;
         ++index) {
        if (pids[index] > 0) {
            job->processes[job->process_count++] = pids[index];
        }
    }
    return id;
}

static struct shell_job *find_job_by_id(int id) {
    for (int i = 0; i < MAX_SHELL_JOBS; ++i) {
        if (shell_jobs[i].state != 0 && shell_jobs[i].id == id) {
            return &shell_jobs[i];
        }
    }
    return NULL;
}

static int write_str(int fd, const char *str) {
    if (!str) return 0;
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

static void print_num(int fd, int val) {
    if (val < 0) {
        write_str(fd, "-");
        val = -val;
    }
    char buf[16];
    size_t idx = 0;
    if (val == 0) {
        buf[idx++] = '0';
    } else {
        char temp[16];
        size_t t = 0;
        while (val > 0) {
            temp[t++] = (char)('0' + (val % 10));
            val /= 10;
        }
        while (t > 0) {
            buf[idx++] = temp[--t];
        }
    }
    buf[idx] = '\0';
    write_str(fd, buf);
}

static void check_completed_jobs(void) {
    for (int i = 0; i < MAX_SHELL_JOBS; ++i) {
        if (shell_jobs[i].state == 1) {
            int all_done = 1;
            int any_stopped = 0;
            for (int p = 0; p < shell_jobs[i].process_count; ++p) {
                int st = 0;
                int ret = waitpid(shell_jobs[i].processes[p], &st, WNOHANG);
                if (ret == 0) {
                    all_done = 0;
                } else if (ret > 0) {
                    if (WIFSTOPPED(st)) {
                        any_stopped = 1;
                        all_done = 0;
                    }
                }
            }
            if (all_done) {
                write_str(STDOUT_FILENO, "[");
                print_num(STDOUT_FILENO, shell_jobs[i].id);
                write_str(STDOUT_FILENO, "]+  Done                    ");
                write_str(STDOUT_FILENO, shell_jobs[i].cmd);
                write_str(STDOUT_FILENO, "\n");
                shell_jobs[i].state = 0;
            } else if (any_stopped) {
                shell_jobs[i].state = 2;
                write_str(STDOUT_FILENO, "[");
                print_num(STDOUT_FILENO, shell_jobs[i].id);
                write_str(STDOUT_FILENO, "]+  Stopped                 ");
                write_str(STDOUT_FILENO, shell_jobs[i].cmd);
                write_str(STDOUT_FILENO, "\n");
            }
        }
    }
}

static void draw_prompt(void) {
    char cwd[128];
    char prompt[320];
    size_t used = 0;
    if (!getcwd(cwd, sizeof(cwd))) {
        strncpy(cwd, "/", sizeof(cwd));
    }
    const char *parts[] = {shell_account.name, "@akaOS:", cwd,
                           geteuid() == 0 ? "# " : "$ "};
    for (size_t part = 0; part < sizeof(parts) / sizeof(parts[0]); ++part) {
        const char *text = parts[part];
        while (*text && used + 1 < sizeof(prompt)) prompt[used++] = *text++;
    }
    prompt[used] = '\0';
    (void)write(STDOUT_FILENO, prompt, used);
}

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
}

static void expand_variables(const char *src, char *dst, size_t max_len) {
    size_t d = 0;
    size_t s = 0;

    while (src[s] && d < max_len - 1) {
        if (src[s] == '$') {
            ++s;
            if (src[s] == '?') {
                ++s;
                char num_buf[16];
                int val = last_exit_status;
                if (val < 0) val = -val;
                size_t n_idx = 0;
                if (val == 0) {
                    num_buf[n_idx++] = '0';
                } else {
                    char temp[16];
                    size_t t = 0;
                    while (val > 0) {
                        temp[t++] = (char)('0' + (val % 10));
                        val /= 10;
                    }
                    while (t > 0) num_buf[n_idx++] = temp[--t];
                }
                num_buf[n_idx] = '\0';
                for (size_t i = 0; i < n_idx && d < max_len - 1; ++i) {
                    dst[d++] = num_buf[i];
                }
                continue;
            }

            if (src[s] == '#') {
                ++s;
                char num_buf[16];
                int val = script_argc;
                snprintf(num_buf, sizeof(num_buf), "%d", val);
                for (size_t i = 0; num_buf[i] && d < max_len - 1; ++i) {
                    dst[d++] = num_buf[i];
                }
                continue;
            }

            if (src[s] == '*' || src[s] == '@') {
                ++s;
                for (int i = 0; i < script_argc; ++i) {
                    if (i > 0 && d < max_len - 1) dst[d++] = ' ';
                    const char *arg = script_args[i];
                    while (*arg && d < max_len - 1) dst[d++] = *arg++;
                }
                continue;
            }

            if (src[s] == '0') {
                ++s;
                const char *val = script_arg0;
                while (*val && d < max_len - 1) dst[d++] = *val++;
                continue;
            }

            if (src[s] >= '1' && src[s] <= '9') {
                int idx = (src[s] - '1');
                ++s;
                if (idx < script_argc) {
                    const char *val = script_args[idx];
                    while (*val && d < max_len - 1) dst[d++] = *val++;
                }
                continue;
            }

            char var_name[32];
            size_t v_len = 0;
            while (is_ident_char(src[s]) && v_len < sizeof(var_name) - 1) {
                var_name[v_len++] = src[s++];
            }
            var_name[v_len] = '\0';

            if (v_len == 0) {
                dst[d++] = '$';
            } else if (strcmp(var_name, "USER") == 0) {
                const char *val = shell_account.name;
                while (*val && d < max_len - 1) dst[d++] = *val++;
            } else if (strcmp(var_name, "HOME") == 0) {
                const char *val = shell_account.home;
                while (*val && d < max_len - 1) dst[d++] = *val++;
            } else if (strcmp(var_name, "PWD") == 0) {
                char cwd[128];
                if (!getcwd(cwd, sizeof(cwd))) strncpy(cwd, "/", sizeof(cwd));
                const char *val = cwd;
                while (*val && d < max_len - 1) dst[d++] = *val++;
            } else if (strcmp(var_name, "ARCH") == 0) {
                const char *val = AKAOS_ARCH_NAME;
                while (*val && d < max_len - 1) dst[d++] = *val++;
            } else {
                const char *val = get_shell_var(var_name);
                if (val) {
                    while (*val && d < max_len - 1) dst[d++] = *val++;
                }
            }
        } else {
            dst[d++] = src[s++];
        }
    }
    dst[d] = '\0';
}

struct command_stage {
    char *argv[MAX_ARGS];
    unsigned char quoted[MAX_ARGS];
    char (*glob_storage)[GLOB_PATH_MAX];
    int argc;
    char *input_file;
    char *output_file;
    int append_output;
};

/* Keep wildcard path storage out of the userspace stack. */
static char shell_glob_storage[MAX_STAGES][MAX_ARGS][GLOB_PATH_MAX];

static void parse_stage(char *str, struct command_stage *stage) {
    stage->argc = 0;
    stage->input_file = NULL;
    stage->output_file = NULL;
    stage->append_output = 0;

    char *p = str;
    while (*p && stage->argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;

        /* Redirections: >>, >, < */
        if (p[0] == '>' && p[1] == '>') {
            p += 2;
            while (*p == ' ' || *p == '\t') ++p;
            char *target = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p) { *p = '\0'; ++p; }
            stage->output_file = target;
            stage->append_output = 1;
            continue;
        } else if (p[0] == '>') {
            p += 1;
            while (*p == ' ' || *p == '\t') ++p;
            char *target = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p) { *p = '\0'; ++p; }
            stage->output_file = target;
            stage->append_output = 0;
            continue;
        } else if (p[0] == '<') {
            p += 1;
            while (*p == ' ' || *p == '\t') ++p;
            char *target = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p) { *p = '\0'; ++p; }
            stage->input_file = target;
            continue;
        }

        /* Argument */
        if (*p == '"') {
            ++p;
            stage->argv[stage->argc] = p;
            stage->quoted[stage->argc++] = 1;
            while (*p && *p != '"') ++p;
            if (*p == '"') {
                *p = '\0';
                ++p;
            }
        } else {
            stage->argv[stage->argc] = p;
            stage->quoted[stage->argc++] = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '<' && *p != '>') ++p;
            if (*p && (*p == ' ' || *p == '\t')) {
                *p = '\0';
                ++p;
            }
        }
    }
    stage->argv[stage->argc] = NULL;
}

static int has_wildcard(const char *text) {
    while (text && *text) {
        if (*text == '*' || *text == '?') return 1;
        ++text;
    }
    return 0;
}

static int wildcard_matches(const char *pattern, const char *name) {
    if (!pattern || !name) return 0;
    if (*pattern == '\0') return *name == '\0';
    if (*pattern == '*') {
        while (pattern[1] == '*') ++pattern;
        if (wildcard_matches(pattern + 1, name)) return 1;
        return *name != '\0' && wildcard_matches(pattern, name + 1);
    }
    if (*pattern == '?') {
        return *name != '\0' && wildcard_matches(pattern + 1, name + 1);
    }
    return *pattern == *name && wildcard_matches(pattern + 1, name + 1);
}

static int store_glob_path(struct command_stage *stage, int slot,
                           const char *prefix, size_t prefix_length,
                           const char *name) {
    size_t name_length = strlen(name);
    if (slot >= MAX_ARGS - 1 ||
        prefix_length + name_length >= GLOB_PATH_MAX) return 0;
    memcpy(stage->glob_storage[slot], prefix, prefix_length);
    memcpy(stage->glob_storage[slot] + prefix_length, name, name_length);
    stage->glob_storage[slot][prefix_length + name_length] = '\0';
    stage->argv[slot] = stage->glob_storage[slot];
    return 1;
}

static int expand_glob_argument(struct command_stage *stage, const char *arg,
                                int output_start) {
    size_t length = strlen(arg);
    size_t slash = length;
    while (slash > 0 && arg[slash - 1] != '/') --slash;

    char directory[GLOB_PATH_MAX];
    const char *pattern = arg + slash;
    size_t prefix_length = slash;
    if (slash == 0) {
        directory[0] = '.';
        directory[1] = '\0';
    } else if (slash == 1) {
        directory[0] = '/';
        directory[1] = '\0';
    } else {
        if (slash >= sizeof(directory)) return 0;
        memcpy(directory, arg, slash - 1);
        directory[slash - 1] = '\0';
    }
    if (has_wildcard(directory) || *pattern == '\0') return 0;

    int descriptor = open(directory, O_RDONLY);
    if (descriptor < 0) return 0;
    int output = output_start;
    struct akaos_dirent entries[16];
    for (;;) {
        ssize_t count = getdents(descriptor, entries, 16);
        if (count <= 0) break;
        for (ssize_t index = 0; index < count; ++index) {
            const char *name = entries[index].name;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            if (name[0] == '.' && pattern[0] != '.') continue;
            if (!wildcard_matches(pattern, name)) continue;
            if (!store_glob_path(stage, output, arg, prefix_length, name)) {
                close(descriptor);
                return output - output_start;
            }
            ++output;
        }
    }
    close(descriptor);
    return output - output_start;
}

static void expand_stage_globs(struct command_stage *stage) {
    char *original[MAX_ARGS];
    unsigned char quoted[MAX_ARGS];
    int original_count = stage->argc;
    for (int index = 0; index < original_count; ++index) {
        original[index] = stage->argv[index];
        quoted[index] = stage->quoted[index];
    }

    int output = 0;
    for (int index = 0; index < original_count && output < MAX_ARGS - 1;
         ++index) {
        int matches = 0;
        if (!quoted[index] && has_wildcard(original[index])) {
            matches = expand_glob_argument(stage, original[index], output);
        }
        if (matches > 0) {
            output += matches;
        } else {
            stage->argv[output++] = original[index];
        }
    }
    stage->argc = output;
    stage->argv[output] = NULL;
}

static long long parse_num_val(const char *str) {
    if (!str) return 0;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }
    long long val = 0;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + (*str - '0');
        str++;
    }
    return sign * val;
}

static int eval_unary_test(const char *op, const char *arg) {
    if (!op || !arg) return 1;
    struct akaos_stat st;
    if (strcmp(op, "-f") == 0) {
        return (stat(arg, &st) == 0 && S_ISREG(st.mode)) ? 0 : 1;
    } else if (strcmp(op, "-d") == 0) {
        return (stat(arg, &st) == 0 && S_ISDIR(st.mode)) ? 0 : 1;
    } else if (strcmp(op, "-e") == 0) {
        return (stat(arg, &st) == 0) ? 0 : 1;
    } else if (strcmp(op, "-s") == 0) {
        return (stat(arg, &st) == 0 && st.size > 0) ? 0 : 1;
    } else if (strcmp(op, "-z") == 0) {
        return (arg[0] == '\0') ? 0 : 1;
    } else if (strcmp(op, "-n") == 0) {
        return (arg[0] != '\0') ? 0 : 1;
    }
    return 1;
}

static int eval_binary_test(const char *left, const char *op, const char *right) {
    if (!left || !op || !right) return 1;
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
        return strcmp(left, right) == 0 ? 0 : 1;
    } else if (strcmp(op, "!=") == 0) {
        return strcmp(left, right) != 0 ? 0 : 1;
    } else if (strcmp(op, "-eq") == 0) {
        return (parse_num_val(left) == parse_num_val(right)) ? 0 : 1;
    } else if (strcmp(op, "-ne") == 0) {
        return (parse_num_val(left) != parse_num_val(right)) ? 0 : 1;
    } else if (strcmp(op, "-lt") == 0) {
        return (parse_num_val(left) < parse_num_val(right)) ? 0 : 1;
    } else if (strcmp(op, "-gt") == 0) {
        return (parse_num_val(left) > parse_num_val(right)) ? 0 : 1;
    } else if (strcmp(op, "-le") == 0) {
        return (parse_num_val(left) <= parse_num_val(right)) ? 0 : 1;
    } else if (strcmp(op, "-ge") == 0) {
        return (parse_num_val(left) >= parse_num_val(right)) ? 0 : 1;
    }
    return 1;
}

static int execute_builtin(int argc, char *argv[]) {
    if (argc == 0) return 0;

    /* Variable assignment: VAR=VAL */
    if (argc == 1 && strchr(argv[0], '=')) {
        char *eq = strchr(argv[0], '=');
        *eq = '\0';
        set_shell_var(argv[0], eq + 1);
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "export") == 0) {
        if (argc > 1) {
            char *eq = strchr(argv[1], '=');
            if (eq) {
                *eq = '\0';
                set_shell_var(argv[1], eq + 1);
            }
        }
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "true") == 0) {
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "false") == 0) {
        last_exit_status = 1;
        return 1;
    }

    if (strcmp(argv[0], "echo") == 0) {
        int no_nl = 0;
        int idx = 1;
        if (idx < argc && strcmp(argv[idx], "-n") == 0) {
            no_nl = 1;
            idx++;
        }
        for (int i = idx; i < argc; ++i) {
            if (i > idx) write_str(STDOUT_FILENO, " ");
            write_str(STDOUT_FILENO, argv[i]);
        }
        if (!no_nl) write_str(STDOUT_FILENO, "\n");
        last_exit_status = 0;
        return 1;
    }

    /* Built-in test and [ */
    if (strcmp(argv[0], "[") == 0 || strcmp(argv[0], "test") == 0) {
        int is_bracket = (strcmp(argv[0], "[") == 0);
        int effective_argc = argc;
        if (is_bracket) {
            if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
                write_str(STDERR_FILENO, "[: missing ']'\n");
                last_exit_status = 2;
                return 1;
            }
            effective_argc--;
        }
        int start = 1;
        int negate = 0;
        if (start < effective_argc && strcmp(argv[start], "!") == 0) {
            negate = 1;
            start++;
        }
        int rem = effective_argc - start;
        int res = 1;
        if (rem == 0) {
            res = 1;
        } else if (rem == 1) {
            res = (argv[start][0] != '\0') ? 0 : 1;
        } else if (rem == 2) {
            res = eval_unary_test(argv[start], argv[start + 1]);
        } else if (rem >= 3) {
            res = eval_binary_test(argv[start], argv[start + 1], argv[start + 2]);
        }
        if (negate) {
            res = (res == 0) ? 1 : 0;
        }
        last_exit_status = res;
        return 1;
    }

    if (strcmp(argv[0], "exit") == 0) {
        int code = last_exit_status;
        if (argc > 1) {
            code = (int)parse_num_val(argv[1]);
        }
        sync();
        _exit(code);
    }

    if (strcmp(argv[0], "shutdown") == 0) {
        if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
            write_str(STDOUT_FILENO, "Usage: shutdown\nPower off the system and shut down hardware.\n");
            last_exit_status = 0;
            return 1;
        }
        if (argc != 1) {
            write_str(STDERR_FILENO, "shutdown: invalid option or extra operand\n");
            last_exit_status = 1;
            return 1;
        }
        if (reboot(RB_POWER_OFF) != 0) {
            write_str(STDERR_FILENO, "shutdown: Permission denied\n");
        }
        last_exit_status = 1;
        return 1;
    }

    if (strcmp(argv[0], "help") == 0) {
        if (argc != 1) {
            write_str(STDERR_FILENO, "help: no arguments supported\n");
            last_exit_status = 1;
            return 1;
        }
        write_str(STDOUT_FILENO, "akash - native userspace shell\n");
        write_str(STDOUT_FILENO, "Built-in commands: cd, exit, help, about, version, id, hostname, shutdown, jobs, fg, bg, test, [, echo, export\n");
        write_str(STDOUT_FILENO, "Utilities: ls, cat, grep, head, tail, wc, ps, tty, stty, uname, whoami, touch, mkdir, rm, chmod, chown, ted, sync, reboot, poweroff, hello, clear, less, more, find, sort, uniq, xargs\n");
        write_str(STDOUT_FILENO, "Scripting: if/then/elif/else/fi, for/do/done, while/do/done, ;, &&, ||, variables, positional parameters ($0-$9, $#, $*, $@)\n");
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "about") == 0) {
        if (argc != 1) {
            write_str(STDERR_FILENO, "about: no arguments supported\n");
            last_exit_status = 1;
            return 1;
        }
        write_str(STDOUT_FILENO, "akaOS " AKAOS_VERSION "\n");
        write_str(STDOUT_FILENO, "An independent operating system\n");
        write_str(STDOUT_FILENO, "Powered by the original XSCore kernel " XSCORE_VERSION "\n");
        write_str(STDOUT_FILENO, "Not Linux and not based on Linux\n");
        write_str(STDOUT_FILENO, "Architecture: ");
        write_str(STDOUT_FILENO, AKAOS_ARCH_NAME);
        write_str(STDOUT_FILENO, "\n");
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "version") == 0) {
        if (argc != 1) {
            write_str(STDERR_FILENO, "version: no arguments supported\n");
            last_exit_status = 1;
            return 1;
        }
        write_str(STDOUT_FILENO, "akaOS " AKAOS_VERSION "\n");
        write_str(STDOUT_FILENO, "Kernel:       XSCore " XSCORE_VERSION "\n");
        write_str(STDOUT_FILENO, "Shell:        akash (Ring 3 userspace)\n");
        write_str(STDOUT_FILENO, "Architecture: ");
        write_str(STDOUT_FILENO, AKAOS_ARCH_NAME);
        write_str(STDOUT_FILENO, " (");
        print_num(STDOUT_FILENO, AKAOS_ARCH_BITS);
        write_str(STDOUT_FILENO, "-bit)\n");
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "id") == 0) {
        if (argc != 1) {
            write_str(STDERR_FILENO, "id: invalid option or extra operand\n");
            last_exit_status = 1;
            return 1;
        }
        write_str(STDOUT_FILENO, "uid=");
        print_num(STDOUT_FILENO, (int)getuid());
        write_str(STDOUT_FILENO, "(");
        write_str(STDOUT_FILENO, shell_account.name);
        write_str(STDOUT_FILENO, ") gid=");
        print_num(STDOUT_FILENO, (int)getgid());
        write_str(STDOUT_FILENO, " euid=");
        print_num(STDOUT_FILENO, (int)geteuid());
        write_str(STDOUT_FILENO, "\n");
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "hostname") == 0) {
        if (argc != 1) {
            write_str(STDERR_FILENO, "hostname: invalid option or extra operand\n");
            last_exit_status = 1;
            return 1;
        }
        write_str(STDOUT_FILENO, "akaOS\n");
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "cd") == 0) {
        if (argc > 2) {
            write_str(STDERR_FILENO, "cd: too many arguments\n");
            last_exit_status = 1;
            return 1;
        }
        if (argc == 2 && strcmp(argv[1], "-") == 0) {
            write_str(STDERR_FILENO, "cd: '-' is not supported\n");
            last_exit_status = 1;
            return 1;
        }
        const char *target = (argc > 1) ? argv[1] : shell_account.home;
        if (chdir(target) != 0) {
            write_str(STDERR_FILENO, "cd: ");
            write_str(STDERR_FILENO, target);
            if (errno == ENOENT) {
                write_str(STDERR_FILENO, ": No such file or directory\n");
            } else if (errno == ENOTDIR) {
                write_str(STDERR_FILENO, ": Not a directory\n");
            } else {
                write_str(STDERR_FILENO, ": Cannot change directory\n");
            }
            last_exit_status = 1;
        } else {
            last_exit_status = 0;
        }
        return 1;
    }

    if (strcmp(argv[0], "jobs") == 0) {
        int show_pids = 0;
        int pids_only = 0;
        if (argc > 1) {
            if (strcmp(argv[1], "-l") == 0) show_pids = 1;
            else if (strcmp(argv[1], "-p") == 0) pids_only = 1;
            else {
                write_str(STDERR_FILENO, "jobs: invalid option: ");
                write_str(STDERR_FILENO, argv[1]);
                write_str(STDERR_FILENO, "\n");
                last_exit_status = 1;
                return 1;
            }
        }
        for (int i = 0; i < MAX_SHELL_JOBS; ++i) {
            if (shell_jobs[i].state == 1) {
                if (pids_only) {
                    print_num(STDOUT_FILENO, shell_jobs[i].pid);
                    write_str(STDOUT_FILENO, "\n");
                } else {
                    write_str(STDOUT_FILENO, "[");
                    print_num(STDOUT_FILENO, shell_jobs[i].id);
                    write_str(STDOUT_FILENO, "]  ");
                    if (show_pids) {
                        print_num(STDOUT_FILENO, shell_jobs[i].pid);
                        write_str(STDOUT_FILENO, " ");
                    }
                    write_str(STDOUT_FILENO, "Running    ");
                    write_str(STDOUT_FILENO, shell_jobs[i].cmd);
                    write_str(STDOUT_FILENO, " &\n");
                }
            } else if (shell_jobs[i].state == 2) {
                if (pids_only) {
                    print_num(STDOUT_FILENO, shell_jobs[i].pid);
                    write_str(STDOUT_FILENO, "\n");
                } else {
                    write_str(STDOUT_FILENO, "[");
                    print_num(STDOUT_FILENO, shell_jobs[i].id);
                    write_str(STDOUT_FILENO, "]+ ");
                    if (show_pids) {
                        print_num(STDOUT_FILENO, shell_jobs[i].pid);
                        write_str(STDOUT_FILENO, " ");
                    }
                    write_str(STDOUT_FILENO, "Stopped    ");
                    write_str(STDOUT_FILENO, shell_jobs[i].cmd);
                    write_str(STDOUT_FILENO, "\n");
                }
            }
        }
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "fg") == 0) {
        int jid = 1;
        if (argc > 1) {
            const char *arg = argv[1];
            if (arg[0] == '%') ++arg;
            jid = 0;
            while (*arg >= '0' && *arg <= '9') {
                jid = jid * 10 + (*arg - '0');
                ++arg;
            }
        }
        struct shell_job *job = find_job_by_id(jid);
        if (!job) {
            write_str(STDERR_FILENO, "fg: no such job\n");
            last_exit_status = 1;
            return 1;
        }
        write_str(STDOUT_FILENO, job->cmd);
        write_str(STDOUT_FILENO, "\n");
        if (tcsetpgrp(STDIN_FILENO, job->pgid) != 0) {
            write_str(STDERR_FILENO, "fg: cannot give terminal to job\n");
            last_exit_status = 1;
            return 1;
        }
        if (job->state == 2) {
            for (int index = 0; index < job->process_count; ++index) {
                (void)kill(job->processes[index], SIGCONT);
            }
            job->state = 1;
        }
        int status = 0;
        int stopped = 0;
        for (int index = 0; index < job->process_count; ++index) {
            int process_status = 0;
            if (waitpid(job->processes[index], &process_status, 0) > 0) {
                status = process_status;
                if (WIFSTOPPED(process_status)) stopped = 1;
            }
        }
        (void)tcsetpgrp(STDIN_FILENO, getpgrp());
        if (stopped) {
            job->state = 2;
            write_str(STDOUT_FILENO, "\n[");
            print_num(STDOUT_FILENO, job->id);
            write_str(STDOUT_FILENO, "]+ Stopped    ");
            write_str(STDOUT_FILENO, job->cmd);
            write_str(STDOUT_FILENO, "\n");
            last_exit_status = 148;
        } else {
            job->state = 0;
            last_exit_status = wait_status_exit_code(status);
        }
        return 1;
    }

    if (strcmp(argv[0], "bg") == 0) {
        int jid = 1;
        if (argc > 1) {
            const char *arg = argv[1];
            if (arg[0] == '%') ++arg;
            jid = 0;
            while (*arg >= '0' && *arg <= '9') {
                jid = jid * 10 + (*arg - '0');
                ++arg;
            }
        }
        struct shell_job *job = find_job_by_id(jid);
        if (!job) {
            write_str(STDERR_FILENO, "bg: no such job\n");
            last_exit_status = 1;
            return 1;
        }
        for (int index = 0; index < job->process_count; ++index) {
            (void)kill(job->processes[index], SIGCONT);
        }
        job->state = 1;
        write_str(STDOUT_FILENO, "[");
        print_num(STDOUT_FILENO, job->id);
        write_str(STDOUT_FILENO, "] ");
        write_str(STDOUT_FILENO, job->cmd);
        write_str(STDOUT_FILENO, " &\n");
        last_exit_status = 0;
        return 1;
    }

    if (strcmp(argv[0], "wait") == 0) {
        if (argc > 1) {
            for (int i = 1; i < argc; ++i) {
                pid_t target_pid = (pid_t)atoi(argv[i]);
                int st = 0;
                if (target_pid > 0) {
                    waitpid(target_pid, &st, 0);
                }
            }
        } else {
            for (int i = 0; i < MAX_SHELL_JOBS; ++i) {
                if (shell_jobs[i].state != 0 && shell_jobs[i].pid > 0) {
                    int st = 0;
                    waitpid(shell_jobs[i].pid, &st, 0);
                    shell_jobs[i].state = 0;
                    shell_jobs[i].pid = 0;
                }
            }
        }
        last_exit_status = 0;
        return 1;
    }

    return -1; /* Not a builtin */
}

static int execute_single_stage(struct command_stage *stage) {
    if (stage->argc == 0) return 0;

    int background = 0;
    if (stage->argc > 0 && strcmp(stage->argv[stage->argc - 1], "&") == 0) {
        background = 1;
        stage->argv[--stage->argc] = NULL;
    }

    int saved_in = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);

    if (stage->input_file) {
        int in_fd = open(stage->input_file, O_RDONLY);
        if (in_fd < 0) {
            write_str(STDERR_FILENO, "akash: cannot open '");
            write_str(STDERR_FILENO, stage->input_file);
            write_str(STDERR_FILENO, "' for reading\n");
            last_exit_status = 1;
            close(saved_in);
            close(saved_out);
            return 1;
        }
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }

    if (stage->output_file) {
        int flags = O_WRONLY | O_CREAT | (stage->append_output ? O_APPEND : O_TRUNC);
        int out_fd = open(stage->output_file, flags);
        if (out_fd < 0) {
            write_str(STDERR_FILENO, "akash: cannot open '");
            write_str(STDERR_FILENO, stage->output_file);
            write_str(STDERR_FILENO, "' for writing\n");
            last_exit_status = 1;
            dup2(saved_in, STDIN_FILENO);
            close(saved_in);
            close(saved_out);
            return 1;
        }
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    int b_res = execute_builtin(stage->argc, stage->argv);
    if (b_res >= 0) {
        dup2(saved_in, STDIN_FILENO);
        close(saved_in);
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
        return 1;
    }

    int pid = fork();
    if (pid == 0) {
        (void)setpgid(0, 0);
        if (!background) {
            (void)tcsetpgrp(STDIN_FILENO, getpid());
        }
        execvp(stage->argv[0], stage->argv);
        int exec_error = errno;
        write_str(STDERR_FILENO, stage->argv[0]);
        if (exec_error == ENOENT) {
            write_str(STDERR_FILENO, ": command not found\n");
            _exit(127);
        }
        write_str(STDERR_FILENO, ": ");
        write_str(STDERR_FILENO, strerror(exec_error));
        write_str(STDERR_FILENO, "\n");
        _exit(126);
    }
    if (pid < 0) {
        if (errno == ENOENT) {
            write_str(STDERR_FILENO, stage->argv[0]);
            write_str(STDERR_FILENO, ": command not found\n");
            last_exit_status = 127;
        } else {
            write_str(STDERR_FILENO, stage->argv[0]);
            write_str(STDERR_FILENO, ": execution failed\n");
            last_exit_status = 1;
        }
    } else if (background) {
        (void)setpgid(pid, pid);
        int j = add_shell_job(pid, 1, stage->argv[0]);
        write_str(STDOUT_FILENO, "[");
        print_num(STDOUT_FILENO, j);
        write_str(STDOUT_FILENO, "] ");
        print_num(STDOUT_FILENO, pid);
        write_str(STDOUT_FILENO, "\n");
        last_exit_status = 0;
    } else {
        (void)setpgid(pid, pid);
        (void)tcsetpgrp(STDIN_FILENO, pid);
        int status = 0;
        waitpid(pid, &status, 0);
        (void)tcsetpgrp(STDIN_FILENO, getpgrp());
        if (WIFSTOPPED(status)) {
            int j = add_shell_job(pid, 2, stage->argv[0]);
            write_str(STDOUT_FILENO, "\n[");
            print_num(STDOUT_FILENO, j);
            write_str(STDOUT_FILENO, "]+ Stopped    ");
            write_str(STDOUT_FILENO, stage->argv[0]);
            write_str(STDOUT_FILENO, "\n");
            last_exit_status = 148;
        } else {
            last_exit_status = wait_status_exit_code(status);
        }
    }

    dup2(saved_in, STDIN_FILENO);
    close(saved_in);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);
    return 1;
}

static int execute_pipeline(struct command_stage stages[], int num_stages) {
    if (num_stages == 1) {
        return execute_single_stage(&stages[0]);
    }

    int background = 0;
    struct command_stage *last = &stages[num_stages - 1];
    if (last->argc > 0 && strcmp(last->argv[last->argc - 1], "&") == 0) {
        background = 1;
        last->argv[--last->argc] = NULL;
    }

    int pipes[MAX_STAGES - 1][2];
    for (int i = 0; i < num_stages - 1; ++i) {
        if (pipe(pipes[i]) != 0) {
            write_str(STDERR_FILENO, "akash: pipe creation failed\n");
            for (int j = 0; j < i; ++j) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            last_exit_status = 1;
            return 1;
        }
    }

    int saved_in = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    int pids[MAX_STAGES];
    int pipeline_pgid = 0;

    for (int i = 0; i < num_stages; ++i) {
        if (stages[i].argc == 0) {
            write_str(STDERR_FILENO, "akash: syntax error in pipeline\n");
            last_exit_status = 2;
            pids[i] = -1;
            continue;
        }

        if (i == 0) {
            if (stages[0].input_file) {
                int in_fd = open(stages[0].input_file, O_RDONLY);
                if (in_fd >= 0) {
                    dup2(in_fd, STDIN_FILENO);
                    close(in_fd);
                } else {
                    write_str(STDERR_FILENO, "akash: cannot open input file\n");
                }
            }
        } else {
            dup2(pipes[i - 1][0], STDIN_FILENO);
        }

        if (i == num_stages - 1) {
            if (stages[num_stages - 1].output_file) {
                int flags = O_WRONLY | O_CREAT | (stages[num_stages - 1].append_output ? O_APPEND : O_TRUNC);
                int out_fd = open(stages[num_stages - 1].output_file, flags);
                if (out_fd >= 0) {
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                } else {
                    write_str(STDERR_FILENO, "akash: cannot open output file\n");
                }
            } else {
                dup2(saved_out, STDOUT_FILENO);
            }
        } else {
            dup2(pipes[i][1], STDOUT_FILENO);
        }

        pids[i] = fork();
        if (pids[i] == 0) {
            for (int k = 0; k < num_stages - 1; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            close(saved_in);
            close(saved_out);
            (void)setpgid(0, pipeline_pgid);
            execvp(stages[i].argv[0], stages[i].argv);
            int exec_error = errno;
            write_str(STDERR_FILENO, stages[i].argv[0]);
            if (exec_error == ENOENT) {
                write_str(STDERR_FILENO, ": command not found\n");
                _exit(127);
            }
            write_str(STDERR_FILENO, ": ");
            write_str(STDERR_FILENO, strerror(exec_error));
            write_str(STDERR_FILENO, "\n");
            _exit(126);
        }
        if (pids[i] < 0) {
            write_str(STDERR_FILENO, stages[i].argv[0]);
            write_str(STDERR_FILENO, ": command not found\n");
        }
        if (pids[i] > 0) {
            if (pipeline_pgid == 0) pipeline_pgid = pids[i];
            (void)setpgid(pids[i], pipeline_pgid);
        }

        dup2(saved_in, STDIN_FILENO);
        dup2(saved_out, STDOUT_FILENO);

        if (i > 0) {
            close(pipes[i - 1][0]);
        }
        if (i < num_stages - 1) {
            close(pipes[i][1]);
        }
    }

    close(saved_in);
    close(saved_out);

    if (background && pipeline_pgid > 0) {
        int job = add_pipeline_job(pids, num_stages, pipeline_pgid, 1,
                                   stages[0].argv[0]);
        write_str(STDOUT_FILENO, "[");
        print_num(STDOUT_FILENO, job);
        write_str(STDOUT_FILENO, "] ");
        print_num(STDOUT_FILENO, pipeline_pgid);
        write_str(STDOUT_FILENO, "\n");
        last_exit_status = 0;
        return 1;
    }

    if (pipeline_pgid > 0) {
        (void)tcsetpgrp(STDIN_FILENO, pipeline_pgid);
    }

    int pipeline_stopped = 0;
    for (int i = 0; i < num_stages; ++i) {
        if (pids[i] > 0) {
            int st = 0;
            waitpid(pids[i], &st, 0);
            if (WIFSTOPPED(st)) pipeline_stopped = 1;
            if (i == num_stages - 1) {
                last_exit_status = wait_status_exit_code(st);
            }
        } else if (i == num_stages - 1) {
            last_exit_status = 127;
        }
    }

    (void)tcsetpgrp(STDIN_FILENO, getpgrp());

    if (pipeline_stopped) {
        int job = add_pipeline_job(pids, num_stages, pipeline_pgid, 2,
                                   stages[0].argv[0]);
        write_str(STDOUT_FILENO, "\n[");
        print_num(STDOUT_FILENO, job);
        write_str(STDOUT_FILENO, "]+ Stopped    ");
        write_str(STDOUT_FILENO, stages[0].argv[0]);
        write_str(STDOUT_FILENO, "\n");
        last_exit_status = 148;
    }

    return 1;
}

static int execute_pipeline_string(char *line) {
    static struct command_stage stages[MAX_STAGES];
    int num_stages = 0;

    char *cursor = line;
    while (*cursor && num_stages < MAX_STAGES) {
        char *stage_start = cursor;
        while (*cursor && *cursor != '|') ++cursor;
        if (*cursor == '|') {
            *cursor = '\0';
            ++cursor;
        }
        stages[num_stages].glob_storage = shell_glob_storage[num_stages];
        parse_stage(stage_start, &stages[num_stages]);
        if (stages[num_stages].argc > 0) {
            expand_stage_globs(&stages[num_stages]);
            ++num_stages;
        }
    }

    if (num_stages == 0) return 0;
    return execute_pipeline(stages, num_stages);
}

static char *trim_str(char *str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

static int execute_command_line(const char *cmd_line) {
    if (!cmd_line || !*cmd_line) return 0;

    char expanded[2048];
    expand_variables(cmd_line, expanded, sizeof(expanded));

    int should_run = 1;
    char sub_cmd[512];
    int sub_len = 0;
    int in_quote = 0;

    const char *p = expanded;
    while (*p) {
        if (*p == '"') {
            in_quote = !in_quote;
            if (sub_len < (int)sizeof(sub_cmd) - 1) sub_cmd[sub_len++] = *p;
            p++;
            continue;
        }

        if (!in_quote) {
            int is_and = (p[0] == '&' && p[1] == '&');
            int is_or  = (p[0] == '|' && p[1] == '|');
            int is_semi = (p[0] == ';');
            int is_bg   = (p[0] == '&' && p[1] != '&');

            if (is_and || is_or || is_semi || is_bg) {
                if (is_bg && sub_len < (int)sizeof(sub_cmd) - 3) {
                    sub_cmd[sub_len++] = ' ';
                    sub_cmd[sub_len++] = '&';
                }
                sub_cmd[sub_len] = '\0';
                char *trimmed = trim_str(sub_cmd);
                if (*trimmed && should_run) {
                    execute_pipeline_string(trimmed);
                }

                if (is_and) {
                    should_run = (last_exit_status == 0);
                    p += 2;
                } else if (is_or) {
                    should_run = (last_exit_status != 0);
                    p += 2;
                } else if (is_semi) {
                    should_run = 1;
                    p += 1;
                } else if (is_bg) {
                    should_run = 1;
                    p += 1;
                }
                sub_len = 0;
                continue;
            }
        }

        if (sub_len < (int)sizeof(sub_cmd) - 1) {
            sub_cmd[sub_len++] = *p;
        }
        p++;
    }

    if (sub_len > 0) {
        sub_cmd[sub_len] = '\0';
        char *trimmed = trim_str(sub_cmd);
        if (*trimmed && should_run) {
            execute_pipeline_string(trimmed);
        }
    }
    return 1;
}

/* Statement tokenizer for control flow */
struct statement_list {
    char **items;
    int count;
    int capacity;
};

static char *shell_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, str, len + 1);
    return copy;
}

static void add_statement(struct statement_list *list, const char *str) {
    if (!str || !*str) return;
    if (list->count >= list->capacity) {
        int new_cap = list->capacity == 0 ? 32 : list->capacity * 2;
        char **new_items = (char **)malloc((size_t)new_cap * sizeof(char *));
        if (list->items) {
            memcpy(new_items, list->items, (size_t)list->count * sizeof(char *));
            free(list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = shell_strdup(str);
}

static void free_statements(struct statement_list *list) {
    if (!list) return;
    for (int i = 0; i < list->count; ++i) {
        if (list->items[i]) free(list->items[i]);
    }
    if (list->items) free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void push_parsed_token(struct statement_list *list, char *trimmed) {
    if (!trimmed || !*trimmed) return;
    if (strncmp(trimmed, "then ", 5) == 0) {
        add_statement(list, "then");
        char *rest = trim_str(trimmed + 5);
        if (*rest) push_parsed_token(list, rest);
    } else if (strncmp(trimmed, "do ", 3) == 0) {
        add_statement(list, "do");
        char *rest = trim_str(trimmed + 3);
        if (*rest) push_parsed_token(list, rest);
    } else {
        size_t tlen = strlen(trimmed);
        if (tlen > 5 && strcmp(trimmed + tlen - 5, " then") == 0) {
            trimmed[tlen - 5] = '\0';
            char *head = trim_str(trimmed);
            if (*head) push_parsed_token(list, head);
            add_statement(list, "then");
        } else if (tlen > 3 && strcmp(trimmed + tlen - 3, " do") == 0) {
            trimmed[tlen - 3] = '\0';
            char *head = trim_str(trimmed);
            if (*head) push_parsed_token(list, head);
            add_statement(list, "do");
        } else {
            add_statement(list, trimmed);
        }
    }
}

static void tokenize_statements(const char *text, struct statement_list *list) {
    char current[512];
    int cur_len = 0;
    int in_quote = 0;
    int in_comment = 0;

    const char *p = text;
    while (*p) {
        char c = *p++;

        if (in_comment) {
            if (c == '\n') {
                in_comment = 0;
            }
            continue;
        }

        if (c == '"') {
            in_quote = !in_quote;
            if (cur_len < (int)sizeof(current) - 1) current[cur_len++] = c;
            continue;
        }

        if (!in_quote && c == '#') {
            in_comment = 1;
            continue;
        }

        if (!in_quote && (c == '\n' || c == ';')) {
            current[cur_len] = '\0';
            char *trimmed = trim_str(current);
            push_parsed_token(list, trimmed);
            cur_len = 0;
            continue;
        }

        if (cur_len < (int)sizeof(current) - 1) {
            current[cur_len++] = c;
        }
    }

    if (cur_len > 0) {
        current[cur_len] = '\0';
        char *trimmed = trim_str(current);
        push_parsed_token(list, trimmed);
    }
}

static int execute_statements(struct statement_list *list, int start, int end) {
    int i = start;
    while (i < end) {
        char *stmt = list->items[i];
        if (!stmt || stmt[0] == '\0') {
            i++;
            continue;
        }

        /* 1. Conditionals: if CONDITION */
        if (strncmp(stmt, "if ", 3) == 0 || strcmp(stmt, "if") == 0) {
            char cond[256];
            cond[0] = '\0';
            if (strncmp(stmt, "if ", 3) == 0) {
                strncpy(cond, stmt + 3, sizeof(cond) - 1);
                cond[sizeof(cond) - 1] = '\0';
            }

            int fi_idx = -1;
            int depth = 0;
            for (int k = i + 1; k < end; ++k) {
                if (strncmp(list->items[k], "if ", 3) == 0 || strcmp(list->items[k], "if") == 0) {
                    depth++;
                } else if (strcmp(list->items[k], "fi") == 0) {
                    if (depth == 0) {
                        fi_idx = k;
                        break;
                    }
                    depth--;
                }
            }
            if (fi_idx < 0) {
                write_str(STDERR_FILENO, "akash: syntax error: missing fi\n");
                last_exit_status = 2;
                return 1;
            }

            int then_start = -1, then_end = -1;
            int else_start = -1, else_end = -1;
            depth = 0;
            for (int k = i + 1; k < fi_idx; ++k) {
                if (strncmp(list->items[k], "if ", 3) == 0 || strcmp(list->items[k], "if") == 0) {
                    depth++;
                } else if (strcmp(list->items[k], "fi") == 0) {
                    depth--;
                } else if (depth == 0) {
                    if (strcmp(list->items[k], "then") == 0 && then_start < 0) {
                        then_start = k + 1;
                    } else if (strncmp(list->items[k], "elif ", 5) == 0 || strcmp(list->items[k], "else") == 0) {
                        if (then_end < 0 && then_start >= 0) {
                            then_end = k;
                        }
                    }
                }
            }
            if (then_start >= 0 && then_end < 0) then_end = fi_idx;

            execute_command_line(cond);
            int matched = (last_exit_status == 0);

            if (matched) {
                if (then_start >= 0 && then_end > then_start) {
                    execute_statements(list, then_start, then_end);
                }
            } else {
                int handled = 0;
                depth = 0;
                for (int k = i + 1; k < fi_idx; ++k) {
                    if (strncmp(list->items[k], "if ", 3) == 0 || strcmp(list->items[k], "if") == 0) {
                        depth++;
                    } else if (strcmp(list->items[k], "fi") == 0) {
                        depth--;
                    } else if (depth == 0) {
                        if (strncmp(list->items[k], "elif ", 5) == 0) {
                            if (!handled) {
                                char elif_cond[256];
                                strncpy(elif_cond, list->items[k] + 5, sizeof(elif_cond) - 1);
                                elif_cond[sizeof(elif_cond) - 1] = '\0';

                                int e_then_start = -1, e_then_end = -1;
                                int sub_depth = 0;
                                for (int m = k + 1; m < fi_idx; ++m) {
                                    if (strncmp(list->items[m], "if ", 3) == 0 || strcmp(list->items[m], "if") == 0) sub_depth++;
                                    else if (strcmp(list->items[m], "fi") == 0) sub_depth--;
                                    else if (sub_depth == 0) {
                                        if (strcmp(list->items[m], "then") == 0 && e_then_start < 0) {
                                            e_then_start = m + 1;
                                        } else if ((strncmp(list->items[m], "elif ", 5) == 0 || strcmp(list->items[m], "else") == 0) && e_then_start >= 0) {
                                            e_then_end = m;
                                            break;
                                        }
                                    }
                                }
                                if (e_then_start >= 0 && e_then_end < 0) e_then_end = fi_idx;

                                execute_command_line(elif_cond);
                                if (last_exit_status == 0) {
                                    handled = 1;
                                    if (e_then_start >= 0 && e_then_end > e_then_start) {
                                        execute_statements(list, e_then_start, e_then_end);
                                    }
                                }
                            }
                        } else if (strcmp(list->items[k], "else") == 0) {
                            else_start = k + 1;
                            else_end = fi_idx;
                            if (!handled) {
                                handled = 1;
                                if (else_start < else_end) {
                                    execute_statements(list, else_start, else_end);
                                }
                            }
                            break;
                        }
                    }
                }
            }

            i = fi_idx + 1;
            continue;
        }

        /* 2. Loops: for VAR in WORDS */
        if (strncmp(stmt, "for ", 4) == 0) {
            char header[256];
            strncpy(header, stmt + 4, sizeof(header) - 1);
            header[sizeof(header) - 1] = '\0';

            int done_idx = -1;
            int depth = 0;
            for (int k = i + 1; k < end; ++k) {
                if (strncmp(list->items[k], "for ", 4) == 0 || strncmp(list->items[k], "while ", 6) == 0) {
                    depth++;
                } else if (strcmp(list->items[k], "done") == 0) {
                    if (depth == 0) {
                        done_idx = k;
                        break;
                    }
                    depth--;
                }
            }
            if (done_idx < 0) {
                write_str(STDERR_FILENO, "akash: syntax error: missing done\n");
                last_exit_status = 2;
                return 1;
            }

            int do_idx = -1;
            depth = 0;
            for (int k = i + 1; k < done_idx; ++k) {
                if (strncmp(list->items[k], "for ", 4) == 0 || strncmp(list->items[k], "while ", 6) == 0) depth++;
                else if (strcmp(list->items[k], "done") == 0) depth--;
                else if (depth == 0 && strcmp(list->items[k], "do") == 0) {
                    do_idx = k;
                    break;
                }
            }
            if (do_idx < 0) {
                write_str(STDERR_FILENO, "akash: syntax error: missing do\n");
                last_exit_status = 2;
                return 1;
            }

            char var_name[32];
            var_name[0] = '\0';
            char *hp = trim_str(header);
            char *space = strchr(hp, ' ');
            if (space) {
                *space = '\0';
                strncpy(var_name, hp, sizeof(var_name) - 1);
                var_name[sizeof(var_name) - 1] = '\0';
                hp = trim_str(space + 1);
                if (strncmp(hp, "in ", 3) == 0) {
                    hp = trim_str(hp + 3);
                }
            }

            char expanded_words[512];
            expand_variables(hp, expanded_words, sizeof(expanded_words));

            char *wp = expanded_words;
            while (*wp) {
                while (*wp == ' ' || *wp == '\t') wp++;
                if (!*wp) break;
                char *word_start = wp;
                while (*wp && *wp != ' ' && *wp != '\t') wp++;
                char saved = *wp;
                *wp = '\0';

                set_shell_var(var_name, word_start);
                execute_statements(list, do_idx + 1, done_idx);

                if (saved) *wp++ = saved;
            }

            i = done_idx + 1;
            continue;
        }

        /* 3. Loops: while CONDITION */
        if (strncmp(stmt, "while ", 6) == 0) {
            char cond[256];
            strncpy(cond, stmt + 6, sizeof(cond) - 1);
            cond[sizeof(cond) - 1] = '\0';

            int done_idx = -1;
            int depth = 0;
            for (int k = i + 1; k < end; ++k) {
                if (strncmp(list->items[k], "for ", 4) == 0 || strncmp(list->items[k], "while ", 6) == 0) {
                    depth++;
                } else if (strcmp(list->items[k], "done") == 0) {
                    if (depth == 0) {
                        done_idx = k;
                        break;
                    }
                    depth--;
                }
            }
            if (done_idx < 0) {
                write_str(STDERR_FILENO, "akash: syntax error: missing done\n");
                last_exit_status = 2;
                return 1;
            }

            int do_idx = -1;
            depth = 0;
            for (int k = i + 1; k < done_idx; ++k) {
                if (strncmp(list->items[k], "for ", 4) == 0 || strncmp(list->items[k], "while ", 6) == 0) depth++;
                else if (strcmp(list->items[k], "done") == 0) depth--;
                else if (depth == 0 && strcmp(list->items[k], "do") == 0) {
                    do_idx = k;
                    break;
                }
            }
            if (do_idx < 0) {
                write_str(STDERR_FILENO, "akash: syntax error: missing do\n");
                last_exit_status = 2;
                return 1;
            }

            while (1) {
                execute_command_line(cond);
                if (last_exit_status != 0) break;
                execute_statements(list, do_idx + 1, done_idx);
            }

            i = done_idx + 1;
            continue;
        }

        /* 4. Normal command line */
        execute_command_line(stmt);
        i++;
    }
    return 0;
}

static int execute_buffer(const char *buf) {
    struct statement_list list;
    list.items = NULL;
    list.count = 0;
    list.capacity = 0;
    tokenize_statements(buf, &list);
    if (list.count > 0) {
        execute_statements(&list, 0, list.count);
    }
    free_statements(&list);
    return last_exit_status;
}

int main(int argc, char **argv) {
    (void)signal(SIGINT, shell_sigint_handler);

    if (!akaos_account_by_uid(geteuid(), &shell_account)) {
        strncpy(shell_account.name, "unknown", sizeof(shell_account.name));
        strncpy(shell_account.home, "/", sizeof(shell_account.home));
        strncpy(shell_account.shell, "/bin/akash", sizeof(shell_account.shell));
        shell_account.uid = geteuid();
        shell_account.gid = getegid();
    }

    /* Check for -c "commands" */
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        if (argc > 2) {
            strncpy(script_arg0, argv[0], sizeof(script_arg0) - 1);
            script_arg0[sizeof(script_arg0) - 1] = '\0';
            script_argc = (argc > 3) ? argc - 3 : 0;
            if (script_argc > (int)(sizeof(script_args) / sizeof(script_args[0]))) {
                script_argc = (int)(sizeof(script_args) / sizeof(script_args[0]));
            }
            for (int i = 0; i < script_argc && i < 10; ++i) {
                strncpy(script_args[i], argv[3 + i], sizeof(script_args[i]) - 1);
                script_args[i][sizeof(script_args[i]) - 1] = '\0';
            }
            return execute_buffer(argv[2]);
        }
        return 0;
    }

    /* Check for script file execution: akash script.sh [arg1 arg2 ...] */
    if (argc > 1 && argv[1][0] != '-') {
        const char *script_file = argv[1];
        int fd = open(script_file, O_RDONLY);
        if (fd < 0) {
            write_str(STDERR_FILENO, "akash: cannot open ");
            write_str(STDERR_FILENO, script_file);
            write_str(STDERR_FILENO, ": No such file or directory\n");
            return 127;
        }
        struct akaos_stat st;
        size_t sz = 8192;
        if (stat(script_file, &st) == 0 && st.size > 0) {
            sz = (size_t)st.size + 1;
        }
        char *buf = (char *)malloc(sz);
        if (!buf) {
            close(fd);
            return 1;
        }
        ssize_t n = read(fd, buf, sz - 1);
        close(fd);
        if (n >= 0) buf[n] = '\0'; else buf[0] = '\0';

        strncpy(script_arg0, script_file, sizeof(script_arg0) - 1);
        script_arg0[sizeof(script_arg0) - 1] = '\0';
        script_argc = (argc > 2) ? argc - 2 : 0;
        if (script_argc > (int)(sizeof(script_args) / sizeof(script_args[0]))) {
            script_argc = (int)(sizeof(script_args) / sizeof(script_args[0]));
        }
        for (int i = 0; i < script_argc && i < 10; ++i) {
            strncpy(script_args[i], argv[2 + i], sizeof(script_args[i]) - 1);
            script_args[i][sizeof(script_args[i]) - 1] = '\0';
        }

        execute_buffer(buf);
        free(buf);
        return last_exit_status;
    }

    /* Become interactive job-control group for login session */
    (void)setpgid(0, 0);
    (void)tcsetpgrp(STDIN_FILENO, getpgrp());

    strncpy(script_arg0, (argc > 0) ? argv[0] : "akash", sizeof(script_arg0) - 1);
    script_arg0[sizeof(script_arg0) - 1] = '\0';
    script_argc = 0;

    /* Desktop/session probes and normal shell scripts can contain several
     * semicolon-separated commands.  Keep interactive input comfortably
     * above the old 255-byte truncation boundary so the shell never silently
     * drops the final command or argument. */
    char raw_buf[1024];
    size_t raw_used = 0;
    int prompt_needed = 1;

    for (;;) {
        if (prompt_needed) {
            check_completed_jobs();
            draw_prompt();
            prompt_needed = 0;
        }

        char input[64];
        ssize_t n = read(STDIN_FILENO, input, sizeof(input));
        if (n < 0) {
            /* A caught terminal signal interrupts the blocking TTY read.
             * Discard the partial command and redraw a clean prompt. */
            raw_used = 0;
            prompt_needed = 1;
            continue;
        }
        if (n == 0) {
            /* An empty canonical read is the terminal's EOF indication
             * (Ctrl+D at an empty prompt).  Leave the shell cleanly instead
             * of spinning forever with a dead keyboard prompt. */
            sync();
            return 0;
        }

        for (ssize_t index = 0; index < n; ++index) {
            char c = input[index];
            if (c == '\n' || c == '\r') {
                raw_buf[raw_used] = '\0';
                if (raw_used > 0) {
                    execute_buffer(raw_buf);
                }
                raw_used = 0;
                prompt_needed = 1;
                continue;
            }
            if ((c == '\b' || c == 0x7f) && raw_used > 0) {
                --raw_used;
            } else if (c != '\b' && c != 0x7f &&
                       raw_used + 1 < sizeof(raw_buf)) {
                raw_buf[raw_used++] = c;
            }
        }
    }
    return 0;
}
