/* A small dynamically-linked smoke program.  The linker records both libc.so.6
 * and libakaos-demo.so as DT_NEEDED; XSCore's native interpreter maps those
 * images, resolves the libc calls below, and then jumps into the normal crt0
 * entry.  Keeping the output in the guest makes it impossible for a test to
 * pass while libc is still accidentally linked into the executable. */
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>

extern int akaos_shared_answer(void);

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    static const char path[] = "/root/.dynamic-libc-check";
    static const char message[] = "dynamic libc shared ABI verified\n";
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (descriptor < 0 ||
        write(descriptor, message, sizeof(message) - 1U) !=
            (ssize_t)(sizeof(message) - 1U) ||
        close(descriptor) < 0) {
        return 1;
    }
    if (akaos_shared_answer() != 42) return 1;
    static const char success[] = "dynamic-loader-ok\n";
    if (write(STDOUT_FILENO, success, sizeof(success) - 1U) !=
        (ssize_t)(sizeof(success) - 1U)) {
        return 1;
    }
    return 0;
}
