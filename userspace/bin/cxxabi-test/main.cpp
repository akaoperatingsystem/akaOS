#include <algorithm>
#include <errno.h>
#include <string>
#include <vector>
#include <new>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static volatile uint32_t global_constructors;
static volatile uint32_t local_constructors;
static volatile uint32_t destructor_order;
static volatile uint32_t allocation_failures;
static volatile uint32_t allocation_failure_detail;
static volatile uint32_t heap_probe_destructors;
static volatile uint32_t exception_scope_destructors;
static volatile uint32_t allocation_new_handler_calls;
static volatile uintptr_t local_addresses[4];
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

struct ExceptionScope {
    ~ExceptionScope() {
        __atomic_add_fetch(&exception_scope_destructors, 1U,
                           __ATOMIC_RELAXED);
    }
};

struct ExceptionBase {
    virtual ~ExceptionBase() = default;
    uint32_t value;
};

struct ExceptionDerived : ExceptionBase {
    explicit ExceptionDerived(uint32_t initial) { value = initial; }
};

__attribute__((noinline)) static void throw_with_unwind(int value) {
    ExceptionScope scope;
    (void)scope;
    throw value;
}

__attribute__((noinline)) static void throw_derived_exception() {
    throw ExceptionDerived(0xC0FFEEU);
}

struct CxxHeapProbe {
    uintptr_t value;
    ~CxxHeapProbe() {
        __atomic_add_fetch(&heap_probe_destructors, 1U, __ATOMIC_RELAXED);
    }
};

struct alignas(64) CxxAlignedProbe {
    uint64_t values[8];
};

struct GlobalProbe {
    GlobalProbe() {
        uint32_t count = __atomic_add_fetch(&global_constructors, 1U,
                                            __ATOMIC_RELAXED);
        printf("C++ global constructor entered: %u\n", count);
    }

    ~GlobalProbe() {
        uint32_t order = __atomic_add_fetch(&destructor_order, 1U,
                                            __ATOMIC_RELAXED);
        puts(order == 3U ? "C++ global destructor finalized last"
                         : "C++ global destructor order failed");
    }
};

struct LocalProbe {
    LocalProbe() {
        __atomic_add_fetch(&local_constructors, 1U, __ATOMIC_RELAXED);
    }

    ~LocalProbe() {
        uint32_t order = __atomic_add_fetch(&destructor_order, 1U,
                                            __ATOMIC_RELAXED);
        puts(order == 2U ? "C++ local-static destructor finalized"
                         : "C++ local-static destructor order failed");
    }
};

static GlobalProbe global_probe;

static LocalProbe &function_local_probe() {
    static LocalProbe probe;
    return probe;
}

static void exit_callback() {
    uint32_t order = __atomic_add_fetch(&destructor_order, 1U,
                                        __ATOMIC_RELAXED);
    puts(order == 1U ? "C++ atexit callback order verified"
                     : "C++ atexit callback order failed");
}

static void record_allocation_failure(uint32_t stage, uintptr_t worker,
                                      uint32_t iteration) {
    uint32_t detail = ((uint32_t)(worker & 0x0fU) << 28) |
                      ((iteration & 0xffffU) << 8) | (stage & 0xffU);
    uint32_t expected = 0U;
    (void)__atomic_compare_exchange_n(&allocation_failure_detail, &expected,
                                      detail, 0, __ATOMIC_RELEASE,
                                      __ATOMIC_RELAXED);
    __atomic_add_fetch(&allocation_failures, 1U, __ATOMIC_RELAXED);
}

static void *touch_function_local(void *argument) {
    uintptr_t index = (uintptr_t)argument;
#if defined(__i386__)
    uint16_t original_x87_control = 0U;
    __asm__ volatile("fnstcw %0" : "=m"(original_x87_control));
    uint16_t worker_x87_control = (uint16_t)(
        (original_x87_control & (uint16_t)~UINT16_C(0x0c00)) |
        (uint16_t)(((index + 1U) & 3U) << 10));
    __asm__ volatile("fldcw %0" : : "m"(worker_x87_control));
#endif
    LocalProbe &probe = function_local_probe();
    if (index < 4U)
        __atomic_store_n(&local_addresses[index], (uintptr_t)&probe,
                         __ATOMIC_RELEASE);

    for (uint32_t iteration = 0U; iteration < 128U; ++iteration) {
        /* Keep distinct first/last sentinels even at the minimum size. */
        size_t size = (size_t)((iteration * 37U + index * 19U) % 510U) + 2U;
        unsigned char *memory = (unsigned char *)malloc(size);
        if (!memory) {
            record_allocation_failure(1U, index, iteration);
            return NULL;
        }
        unsigned char first = (unsigned char)(index + iteration);
        unsigned char last = (unsigned char)(size ^ iteration);
        memory[0] = first;
        memory[size - 1U] = last;
        (void)sched_yield();
#if defined(__i386__)
        uint16_t observed_x87_control = 0U;
        __asm__ volatile("fnstcw %0" : "=m"(observed_x87_control));
        if (((observed_x87_control ^ worker_x87_control) &
             UINT16_C(0x0c00)) != 0U)
            record_allocation_failure(6U, index, iteration);
#endif

        size_t grown_size = size + 73U;
        unsigned char *grown = (unsigned char *)realloc(memory, grown_size);
        if (!grown) {
            free(memory);
            record_allocation_failure(2U, index, iteration);
            return NULL;
        }
        if (grown[0] != first || grown[size - 1U] != last) {
            record_allocation_failure(3U, index, iteration);
        }
        free(grown);

        uintptr_t *array = new (std::nothrow) uintptr_t[8];
        if (!array) {
            record_allocation_failure(4U, index, iteration);
            return NULL;
        }
        for (uintptr_t element = 0U; element < 8U; ++element)
            array[element] = index * 1000U + iteration + element;
        (void)sched_yield();
#if defined(__i386__)
        __asm__ volatile("fnstcw %0" : "=m"(observed_x87_control));
        if (((observed_x87_control ^ worker_x87_control) &
             UINT16_C(0x0c00)) != 0U)
            record_allocation_failure(7U, index, iteration);
#endif
        if (array[0] != index * 1000U + iteration ||
            array[7] != index * 1000U + iteration + 7U)
            record_allocation_failure(5U, index, iteration);
        delete[] array;
    }
#if defined(__i386__)
    __asm__ volatile("fldcw %0" : : "m"(original_x87_control));
#endif
    return NULL;
}

extern "C" int main() {
    if (__stack_chk_guard == 0U ||
        (__stack_chk_guard & (uintptr_t)0xffU) != 0U ||
        stack_protector_probe(0x41U) != 0x21U) {
        puts("C++ static stack protector guard initialization failed");
        return 16;
    }
    puts("C++ static stack protector guard initialized");
    int caught_value = 0;
    try {
        throw_with_unwind(0x5373);
    } catch (int value) {
        caught_value = value;
    }
    if (caught_value != 0x5373 ||
        __atomic_load_n(&exception_scope_destructors, __ATOMIC_ACQUIRE) != 1U) {
        puts("C++ exception catch or stack-unwind destructor failed");
        return 11;
    }
    puts("C++ exception catch and stack-unwind destructor verified");

    uint32_t caught_derived_value = 0U;
    try {
        throw_derived_exception();
    } catch (const ExceptionBase &base) {
        const ExceptionDerived *derived =
            dynamic_cast<const ExceptionDerived *>(&base);
        if (derived) caught_derived_value = derived->value;
    }
    if (caught_derived_value != 0xC0FFEEU) {
        puts("C++ RTTI catch and dynamic_cast failed");
        return 12;
    }
    puts("C++ RTTI catch and dynamic_cast verified");

    std::string standard_text("akaOS libc++ standard-library runtime");
    std::vector<int> standard_values;
    standard_values.reserve(4U);
    standard_values.push_back(4);
    standard_values.push_back(1);
    standard_values.push_back(3);
    standard_values.push_back(2);
    std::sort(standard_values.begin(), standard_values.end());
    if (standard_text != "akaOS libc++ standard-library runtime" ||
        standard_values.size() != 4U || standard_values[0] != 1 ||
        standard_values[1] != 2 || standard_values[2] != 3 ||
        standard_values[3] != 4) {
        puts("C++ libc++ containers, strings, and algorithms failed");
        return 13;
    }
    puts("C++ libc++ containers, strings, and algorithms verified");

    uint32_t global_count =
        __atomic_load_n(&global_constructors, __ATOMIC_ACQUIRE);
    if (global_count != 1U) {
        printf("C++ global constructor check failed: %u\n", global_count);
        return 1;
    }
    puts("C++ global constructor verified");

    CxxHeapProbe *scalar = new CxxHeapProbe;
    scalar->value = (uintptr_t)0x12345678U;
    delete scalar;

    CxxHeapProbe *objects = new CxxHeapProbe[3];
    for (uintptr_t index = 0U; index < 3U; ++index)
        objects[index].value = index + 40U;
    delete[] objects;

    CxxAlignedProbe *aligned = new CxxAlignedProbe;
    aligned->values[0] = UINT64_C(0xA55A5AA5);
    if (((uintptr_t)aligned & (alignof(CxxAlignedProbe) - 1U)) != 0U ||
        aligned->values[0] != UINT64_C(0xA55A5AA5)) {
        puts("C++ aligned allocation failed");
        delete aligned;
        return 2;
    }
    delete aligned;

    CxxAlignedProbe *aligned_nothrow = new (
        static_cast<std::align_val_t>(64U), std::nothrow) CxxAlignedProbe;
    if (!aligned_nothrow ||
        ((uintptr_t)aligned_nothrow & (alignof(CxxAlignedProbe) - 1U)) != 0U) {
        puts("C++ nothrow aligned allocation failed");
        return 3;
    }
    delete aligned_nothrow;

    void *standard_aligned = aligned_alloc(128U, 256U);
    if (!standard_aligned ||
        ((uintptr_t)standard_aligned & (uintptr_t)127U) != 0U) {
        puts("C11 aligned_alloc alignment failed");
        free(standard_aligned);
        return 9;
    }
    free(standard_aligned);
    errno = 0;
    void *invalid_aligned = aligned_alloc(64U, 65U);
    if (invalid_aligned || errno != EINVAL) {
        puts("C11 aligned_alloc size validation failed");
        free(invalid_aligned);
        return 10;
    }
    puts("C11 aligned_alloc/free and validation verified");

    void *overflow = malloc(SIZE_MAX);
    int malloc_overflow_errno = errno;
    void *calloc_overflow = calloc(SIZE_MAX, 2U);
    if (overflow || malloc_overflow_errno != ENOMEM || calloc_overflow ||
        errno != ENOMEM) {
        puts("C++ allocator overflow rejection failed");
        free(overflow);
        free(calloc_overflow);
        return 4;
    }
    if (__atomic_load_n(&heap_probe_destructors, __ATOMIC_ACQUIRE) != 4U) {
        puts("C++ delete destructor count failed");
        return 5;
    }
    puts("C++ global new/delete and over-aligned allocation verified");
    puts("C++ allocator overflow rejection verified");

    std::new_handler previous_new_handler =
        std::set_new_handler(disable_allocation_new_handler);
    int caught_bad_alloc = 0;
    try {
        (void)::operator new(SIZE_MAX);
    } catch (const std::bad_alloc &failure) {
        caught_bad_alloc = failure.what() != NULL;
    }
    (void)std::set_new_handler(previous_new_handler);
    if (!caught_bad_alloc ||
        __atomic_load_n(&allocation_new_handler_calls, __ATOMIC_ACQUIRE) != 1U) {
        puts("C++ throwing new/new_handler failure semantics failed");
        return 14;
    }

    void *nothrow_failure = ::operator new(SIZE_MAX, std::nothrow);
    if (nothrow_failure != NULL) {
        puts("C++ nothrow new failure semantics failed");
        ::operator delete(nothrow_failure);
        return 15;
    }
    puts("C++ std::bad_alloc, new_handler, and nothrow failure verified");

    pthread_t workers[4];
    unsigned created = 0U;
    for (; created < 4U; ++created) {
        if (pthread_create(&workers[created], NULL, touch_function_local,
                           (void *)(uintptr_t)created) != 0)
            break;
    }
    int valid = created == 4U;
    for (unsigned index = 0U; index < created; ++index) {
        void *result = (void *)(uintptr_t)1U;
        if (pthread_join(workers[index], &result) != 0 || result != NULL)
            valid = 0;
    }
    uintptr_t address = __atomic_load_n(&local_addresses[0], __ATOMIC_ACQUIRE);
    for (unsigned index = 1U; index < 4U; ++index) {
        if (__atomic_load_n(&local_addresses[index], __ATOMIC_ACQUIRE) != address)
            valid = 0;
    }
    if (!address ||
        __atomic_load_n(&local_constructors, __ATOMIC_ACQUIRE) != 1U || !valid) {
        printf("C++ local-static diagnostic: workers=%u/4 constructors=%u\n",
               created,
               __atomic_load_n(&local_constructors, __ATOMIC_ACQUIRE));
        puts("C++ thread-safe local-static initialization failed");
        return 6;
    }
    puts("C++ thread-safe local-static initialization verified");
    if (__atomic_load_n(&allocation_failures, __ATOMIC_ACQUIRE) != 0U) {
        uint32_t detail = __atomic_load_n(&allocation_failure_detail,
                                          __ATOMIC_ACQUIRE);
        printf("C++ pthread concurrent allocator stress failed: %08x\n",
               detail);
        return 7;
    }
    puts("C++ pthread concurrent malloc/realloc/new/delete verified");
#if defined(__i386__)
    puts("x86_32 x87 control-state isolation verified");
#endif

    if (atexit(exit_callback) != 0) {
        puts("C++ atexit registration failed");
        return 8;
    }
    puts("C++ ABI constructor/guard tests passed");
    return 0;
}
