#include <algorithm>
#include <link.h>
#include <new>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

static volatile uint32_t allocation_new_handler_calls;
extern "C" uintptr_t __stack_chk_guard;

__attribute__((noinline)) static uint32_t stack_protector_probe(uint8_t seed) {
    volatile uint8_t bytes[32];
    for (uint32_t index = 0U; index < sizeof(bytes); ++index)
        bytes[index] = (uint8_t)(seed + index);
    return (uint32_t)bytes[0] ^ (uint32_t)bytes[31];
}

static void disable_allocation_new_handler() {
    __atomic_add_fetch(&allocation_new_handler_calls, 1U, __ATOMIC_RELAXED);
    (void)std::set_new_handler(nullptr);
}

#if defined(__arm__)
#define AKAOS_TEST_UNWIND_SEGMENT PT_ARM_EXIDX
#else
#define AKAOS_TEST_UNWIND_SEGMENT PT_GNU_EH_FRAME
#endif

struct RuntimeImageReport {
    int saw_program;
    int saw_libcxx;
    int saw_cxxabi;
    int saw_libunwind;
    int program_has_eh_frame;
    int libcxx_has_eh_frame;
    int cxxabi_has_eh_frame;
    int libunwind_has_eh_frame;
    int invalid_image;
};

static int inspect_runtime_images(struct dl_phdr_info *image,
                                  size_t image_info_size, void *context) {
    RuntimeImageReport *report = static_cast<RuntimeImageReport *>(context);
    if (!report || !image || image_info_size < sizeof(*image) ||
        !image->dlpi_name || !image->dlpi_phdr || image->dlpi_phnum == 0) {
        if (report) report->invalid_image = 1;
        return 1;
    }

    int has_load_segment = 0;
    int has_eh_frame = 0;
    for (Elf_Half index = 0; index < image->dlpi_phnum; ++index) {
        if (image->dlpi_phdr[index].p_type == PT_LOAD)
            has_load_segment = 1;
        else if (image->dlpi_phdr[index].p_type == AKAOS_TEST_UNWIND_SEGMENT)
            has_eh_frame = 1;
    }
    if (!has_load_segment) {
        report->invalid_image = 1;
        return 1;
    }

    if (strcmp(image->dlpi_name, "/bin/cxx-dynamic-test") == 0) {
        report->saw_program = 1;
        report->program_has_eh_frame = has_eh_frame;
    } else if (strcmp(image->dlpi_name, "/lib/libc++.so.1") == 0) {
        report->saw_libcxx = 1;
        report->libcxx_has_eh_frame = has_eh_frame;
    } else if (strcmp(image->dlpi_name, "/lib/libc++abi.so.1") == 0) {
        report->saw_cxxabi = 1;
        report->cxxabi_has_eh_frame = has_eh_frame;
    } else if (strcmp(image->dlpi_name, "/lib/libunwind.so.1") == 0) {
        report->saw_libunwind = 1;
        report->libunwind_has_eh_frame = has_eh_frame;
    }
    return 0;
}

extern "C" int main() {
    uintptr_t guard = __stack_chk_guard;
    if (guard == 0U) {
        puts("C++ shared stack protector guard is zero");
        return 6;
    }
    if ((guard & (uintptr_t)0xffU) != 0U) {
        printf("C++ shared stack protector guard low byte is nonzero: %lx\n",
               (unsigned long)guard);
        return 6;
    }
    uint32_t probe = stack_protector_probe(0x41U);
    if (probe != 0x21U) {
        printf("C++ shared stack protector probe mismatch: %x\n", probe);
        return 6;
    }
    puts("C++ shared stack protector guard initialized");
    puts("C++ shared runtime entered main");
    std::string text("akaOS shared libc++ runtime");
    std::vector<int> values;
    values.push_back(9);
    values.push_back(2);
    values.push_back(7);
    values.push_back(1);
    std::sort(values.begin(), values.end());
    if (text != "akaOS shared libc++ runtime" || values.size() != 4U ||
        values[0] != 1 || values[1] != 2 || values[2] != 7 ||
        values[3] != 9) {
        puts("C++ shared libc++ containers failed");
        return 1;
    }
    puts("C++ shared libc++ containers verified");
    if (std::to_string(1.25) != "1.250000" ||
        std::to_wstring(2.5) != L"2.500000") {
        puts("C++ shared libc++ numeric formatting failed");
        return 2;
    }
    puts("C++ shared libc++ numeric formatting verified");

    std::new_handler previous_new_handler =
        std::set_new_handler(disable_allocation_new_handler);
    int caught_bad_alloc = 0;
    try {
        (void)::operator new(SIZE_MAX);
    } catch (const std::bad_alloc &failure) {
        caught_bad_alloc = failure.what() != nullptr;
    }
    (void)std::set_new_handler(previous_new_handler);
    void *nothrow_failure = ::operator new(SIZE_MAX, std::nothrow);
    if (!caught_bad_alloc || nothrow_failure != nullptr ||
        __atomic_load_n(&allocation_new_handler_calls, __ATOMIC_ACQUIRE) != 1U) {
        puts("C++ shared std::bad_alloc/new_handler behavior failed");
        ::operator delete(nothrow_failure);
        return 5;
    }
    puts("C++ shared std::bad_alloc and new_handler semantics verified");

    RuntimeImageReport report = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (dl_iterate_phdr(inspect_runtime_images, &report) != 0 ||
        !report.saw_program || !report.program_has_eh_frame ||
        !report.saw_libcxx || !report.saw_cxxabi ||
        !report.saw_libunwind || !report.libcxx_has_eh_frame ||
        !report.cxxabi_has_eh_frame || !report.libunwind_has_eh_frame ||
        report.invalid_image) {
        puts("C++ shared runtime ELF image enumeration failed");
        return 3;
    }
    puts("C++ shared runtime ELF images and unwind headers verified");

    puts("C++ shared runtime entering exception probe");
    int caught_value = 0;
    try {
        throw 0x5A21;
    } catch (int value) {
        puts("C++ shared runtime catch body entered");
        caught_value = value;
    }
    if (caught_value != 0x5A21) {
        puts("C++ shared-runtime exception/unwind failed");
        return 4;
    }
    puts("C++ shared-runtime exception/unwind verified");
    puts("C++ shared runtime dynamic-link test passed");
    return 0;
}
