#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define AKAOS_AT_EXECFN 31U
#define AKAOS_AT_PHDR 3U
#define AKAOS_AT_PHENT 4U
#define AKAOS_AT_PHNUM 5U
#define AKAOS_AT_PAGESZ 6U
#define AKAOS_AT_ENTRY 9U

static void loader_write(const char *message) {
    if (!message) return;
    (void)write(STDERR_FILENO, message, strlen(message));
}

static int find_aux_value(int argc, char **argv, uintptr_t wanted,
                          uintptr_t *value_out) {
    if (argc < 0 || !argv || !value_out) return 0;
    char **environment = argv + (size_t)argc + 1U;
    while (*environment) ++environment;
    ++environment;
    uintptr_t *auxiliary = (uintptr_t *)(void *)environment;
    for (size_t index = 0; index < 64; index += 2) {
        uintptr_t type = auxiliary[index];
        uintptr_t value = auxiliary[index + 1];
        if (type == 0) break;
        if (type == wanted) {
            *value_out = value;
            return 1;
        }
    }
    return 0;
}

static const char *find_execfn(int argc, char **argv) {
    if (argc < 0 || !argv) return NULL;
    uintptr_t value = 0;
    if (find_aux_value(argc, argv, AKAOS_AT_EXECFN, &value) && value != 0)
        return (const char *)(uintptr_t)value;
    return NULL;
}

static _Noreturn void enter_object(void *entry, uintptr_t stack) {
#if defined(__x86_64__)
    __asm__ volatile("mov %0, %%rsp\n\t"
                     "jmp *%1\n\t" :: "r"(stack), "r"(entry) : "memory");
#elif defined(__i386__)
    __asm__ volatile("mov %0, %%esp\n\t"
                     "jmp *%1\n\t" :: "r"(stack), "r"(entry) : "memory");
#elif defined(__aarch64__)
    __asm__ volatile("mov sp, %0\n\t"
                     "br %1\n\t" :: "r"(stack), "r"(entry) : "memory");
#elif defined(__arm__)
    /* ARM32 crt0 receives the initial stack in r2 as well as SP. */
    __asm__ volatile("mov sp, %0\n\t"
                     "mov r2, %0\n\t"
                     "bx %1\n\t" :: "r"(stack), "r"(entry) : "memory");
#elif defined(__riscv)
    __asm__ volatile("mv sp, %0\n\t"
                     "jr %1\n\t" :: "r"(stack), "r"(entry) : "memory");
#else
#error "Unsupported akaOS interpreter architecture"
#endif
    __builtin_unreachable();
}

int main(int argc, char **argv) {
    uintptr_t program_header = 0;
    uintptr_t program_header_entry_size = 0;
    uintptr_t program_header_count = 0;
    uintptr_t page_size = 0;
    uintptr_t initial_entry = 0;
    int have_phdr = find_aux_value(argc, argv, AKAOS_AT_PHDR, &program_header);
    int have_phent = find_aux_value(argc, argv, AKAOS_AT_PHENT,
                                    &program_header_entry_size);
    int have_phnum = find_aux_value(argc, argv, AKAOS_AT_PHNUM, &program_header_count);
    int have_pagesz = find_aux_value(argc, argv, AKAOS_AT_PAGESZ, &page_size);
    int have_entry = find_aux_value(argc, argv, AKAOS_AT_ENTRY, &initial_entry);
    if (!have_phdr || !have_phent || !have_phnum || !have_pagesz ||
        !have_entry ||
        program_header == 0 || program_header_entry_size == 0 ||
        program_header_count == 0 || page_size != 4096 || initial_entry == 0) {
        loader_write("akaos ld: incomplete ELF auxiliary vector\n");
        return 127;
    }
    const char *path = find_execfn(argc, argv);
    if (!path || !*path) {
        path = (argc > 0 && argv && argv[0]) ? argv[0] : NULL;
    }
    if (!path || !*path) {
        loader_write("akaos ld: missing executable path\n");
        return 127;
    }

    void *object = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!object) {
        loader_write("akaos ld: unable to load executable: ");
        char *detail = dlerror();
        loader_write(detail ? detail : "unknown error");
        loader_write("\n");
        return 127;
    }
    void *entry = akaos_dl_entry(object);
    if (!entry) {
        loader_write("akaos ld: executable has no entry point\n");
        return 127;
    }
    uintptr_t stack = (uintptr_t)(void *)argv - sizeof(uintptr_t);
    enter_object(entry, stack);
}
