#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    execv("/bin/akash", argv);
    return 127;
}
