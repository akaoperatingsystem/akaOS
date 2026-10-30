#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int eval_unary(const char *op, const char *arg) {
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

static long long simple_atoll(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    long long val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}

static int eval_binary(const char *left, const char *op, const char *right) {
    if (!left || !op || !right) return 1;
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
        return strcmp(left, right) == 0 ? 0 : 1;
    } else if (strcmp(op, "!=") == 0) {
        return strcmp(left, right) != 0 ? 0 : 1;
    } else if (strcmp(op, "-eq") == 0) {
        return (simple_atoll(left) == simple_atoll(right)) ? 0 : 1;
    } else if (strcmp(op, "-ne") == 0) {
        return (simple_atoll(left) != simple_atoll(right)) ? 0 : 1;
    } else if (strcmp(op, "-lt") == 0) {
        return (simple_atoll(left) < simple_atoll(right)) ? 0 : 1;
    } else if (strcmp(op, "-gt") == 0) {
        return (simple_atoll(left) > simple_atoll(right)) ? 0 : 1;
    } else if (strcmp(op, "-le") == 0) {
        return (simple_atoll(left) <= simple_atoll(right)) ? 0 : 1;
    } else if (strcmp(op, "-ge") == 0) {
        return (simple_atoll(left) >= simple_atoll(right)) ? 0 : 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc <= 1) return 1; // False if empty

    const char *prog = argv[0];
    size_t prog_len = strlen(prog);
    if (prog_len > 0 && (prog[prog_len - 1] == '[' || strcmp(prog, "[") == 0)) {
        if (strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ']'\n");
            return 2;
        }
        argc--; // Omit trailing ']'
    }

    int start = 1;
    int negate = 0;
    if (start < argc && strcmp(argv[start], "!") == 0) {
        negate = 1;
        start++;
    }

    int remaining = argc - start;
    int result = 1;

    if (remaining == 0) {
        result = 1;
    } else if (remaining == 1) {
        result = (argv[start][0] != '\0') ? 0 : 1;
    } else if (remaining == 2) {
        result = eval_unary(argv[start], argv[start + 1]);
    } else if (remaining == 3) {
        result = eval_binary(argv[start], argv[start + 1], argv[start + 2]);
    } else {
        result = 1;
    }

    if (negate) {
        result = (result == 0) ? 1 : 0;
    }
    return result;
}
