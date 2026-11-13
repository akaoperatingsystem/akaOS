#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int is_number(const char *s) {
    if (*s == '-') s++;
    if (!*s) return 0;
    while (*s) {
        if (!isdigit(*s)) return 0;
        s++;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "expr: missing operand\n");
        return 3;
    }
    if (strcmp(argv[1], "--help") == 0) {
        printf("Usage: expr EXPRESSION\n");
        return 0;
    }
    if (argc == 4) {
        long long a = atoll(argv[1]);
        long long b = atoll(argv[3]);
        if (strcmp(argv[2], "+") == 0) {
            printf("%lld\n", a + b);
            return (a + b) == 0;
        } else if (strcmp(argv[2], "-") == 0) {
            printf("%lld\n", a - b);
            return (a - b) == 0;
        } else if (strcmp(argv[2], "*") == 0) {
            printf("%lld\n", a * b);
            return (a * b) == 0;
        } else if (strcmp(argv[2], "/") == 0) {
            if (b == 0) { fprintf(stderr, "expr: division by zero\n"); return 3; }
            printf("%lld\n", a / b);
            return (a / b) == 0;
        } else if (strcmp(argv[2], "%") == 0) {
            if (b == 0) { fprintf(stderr, "expr: division by zero\n"); return 3; }
            printf("%lld\n", a % b);
            return (a % b) == 0;
        } else if (strcmp(argv[2], "=") == 0 || strcmp(argv[2], "==") == 0) {
            int res = (a == b);
            printf("%d\n", res);
            return !res;
        } else if (strcmp(argv[2], "<") == 0) {
            int res = (a < b);
            printf("%d\n", res);
            return !res;
        } else if (strcmp(argv[2], ">") == 0) {
            int res = (a > b);
            printf("%d\n", res);
            return !res;
        }
    } else if (argc == 3 && strcmp(argv[1], "length") == 0) {
        printf("%lu\n", strlen(argv[2]));
        return strlen(argv[2]) == 0;
    }

    // Default fallback
    for (int i = 1; i < argc; i++) printf("%s%s", argv[i], (i == argc - 1) ? "" : " ");
    printf("\n");
    return 0;
}
