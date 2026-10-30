#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        const char usage[] = "Usage: clear\nClear the terminal screen.\n";
        write(STDOUT_FILENO, usage, sizeof(usage) - 1);
        return 0;
    }
    if (argc != 1) {
        const char error[] = "clear: invalid option or extra operand\n";
        write(STDERR_FILENO, error, sizeof(error) - 1);
        return 1;
    }
    const char clear_seq[] = "\033[2J\033[H";
    write(STDOUT_FILENO, clear_seq, sizeof(clear_seq) - 1);
    return 0;
}
