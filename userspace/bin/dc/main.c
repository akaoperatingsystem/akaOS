#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define STACK_SIZE 256
static double stack[STACK_SIZE];
static int top = -1;

void push(double val) {
    if (top < STACK_SIZE - 1) stack[++top] = val;
    else fprintf(stderr, "dc: stack overflow\n");
}

double pop() {
    if (top >= 0) return stack[top--];
    fprintf(stderr, "dc: stack underflow\n");
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[1024];
    while (scanf("%1023s", buf) != EOF) {
        if (isdigit(buf[0]) || (buf[0] == '_' && isdigit(buf[1]))) {
            if (buf[0] == '_') buf[0] = '-';
            push(atof(buf));
        } else if (strcmp(buf, "+") == 0) {
            push(pop() + pop());
        } else if (strcmp(buf, "-") == 0) {
            double b = pop();
            double a = pop();
            push(a - b);
        } else if (strcmp(buf, "*") == 0) {
            push(pop() * pop());
        } else if (strcmp(buf, "/") == 0) {
            double b = pop();
            double a = pop();
            if (b != 0) push(a / b);
            else fprintf(stderr, "dc: division by zero\n");
        } else if (strcmp(buf, "p") == 0) {
            if (top >= 0) printf("%g\n", stack[top]);
        } else if (strcmp(buf, "q") == 0) {
            return 0;
        } else {
            fprintf(stderr, "dc: unknown command %s\n", buf);
        }
    }
    return 0;
}
