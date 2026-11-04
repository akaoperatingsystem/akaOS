#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static volatile uint32_t akaos_rand_state = UINT32_C(1);

void srand(unsigned int seed) {
    __atomic_store_n(&akaos_rand_state, (uint32_t)seed, __ATOMIC_RELEASE);
}

int rand(void) {
    uint32_t old = __atomic_load_n(&akaos_rand_state, __ATOMIC_ACQUIRE);
    uint32_t next;
    do {
        next = old * UINT32_C(1103515245) + UINT32_C(12345);
    } while (!__atomic_compare_exchange_n(&akaos_rand_state, &old, next,
                                           1, __ATOMIC_ACQ_REL,
                                           __ATOMIC_ACQUIRE));
    return (int)(next >> 1);
}

int rand_r(unsigned int *seed) {
    if (!seed) return 0;
    uint32_t next = (uint32_t)*seed * UINT32_C(1103515245) +
                    UINT32_C(12345);
    *seed = (unsigned int)next;
    return (int)(next >> 1);
}

/* POSIX random() keeps its own state so that seeding it never disturbs the
 * rand() sequence and vice versa.  The generator is a 64-bit xorshift
 * shifted into the 31-bit range POSIX requires. */
static volatile uint64_t akaos_random_state = UINT64_C(0x2545F4914F6CDD1D);

long random(void) {
    uint64_t current = __atomic_load_n(&akaos_random_state, __ATOMIC_ACQUIRE);
    uint64_t next;
    do {
        uint64_t mixed = current;
        mixed ^= mixed >> 12;
        mixed ^= mixed << 25;
        mixed ^= mixed >> 27;
        next = mixed * UINT64_C(2685821657736338717);
        current = mixed;
    } while (!__atomic_compare_exchange_n(&akaos_random_state, &current, next,
                                          1, __ATOMIC_ACQ_REL,
                                          __ATOMIC_ACQUIRE));
    return (long)((next >> 32U) & 0x7FFFFFFFU);
}

void srandom(unsigned int seed) {
    uint64_t state = ((uint64_t)seed << 32U) ^ UINT64_C(0x9E3779B97F4A7C15);
    if (state == 0U) state = UINT64_C(0x2545F4914F6CDD1D);
    __atomic_store_n(&akaos_random_state, state, __ATOMIC_RELEASE);
}

int mkstemp(char *template_path) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static volatile uint32_t sequence = 0U;
    if (!template_path) {
        errno = EINVAL;
        return -1;
    }

    size_t length = strlen(template_path);
    if (length < 6U) {
        errno = EINVAL;
        return -1;
    }
    char *suffix = template_path + length - 6U;
    for (size_t i = 0; i < 6U; ++i) {
        if (suffix[i] != 'X') {
            errno = EINVAL;
            return -1;
        }
    }

    for (uint32_t attempt = 0U; attempt < 256U; ++attempt) {
        uint32_t serial = __atomic_add_fetch(&sequence, 1U, __ATOMIC_RELAXED);
        uint32_t state = serial * UINT32_C(0x9E3779B9) ^
                         (uint32_t)getpid() * UINT32_C(0x85EBCA6B) ^ attempt;
        for (size_t i = 0; i < 6U; ++i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            suffix[i] = alphabet[state % (sizeof(alphabet) - 1U)];
        }

        int descriptor = open(template_path, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0) return descriptor;
        if (errno != EEXIST) return -1;
    }

    errno = EEXIST;
    return -1;
}

char *mkdtemp(char *template_path) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static volatile uint32_t sequence = 0U;
    if (!template_path) {
        errno = EINVAL;
        return NULL;
    }

    size_t length = strlen(template_path);
    if (length < 6U) {
        errno = EINVAL;
        return NULL;
    }
    char *suffix = template_path + length - 6U;
    for (size_t i = 0; i < 6U; ++i) {
        if (suffix[i] != 'X') {
            errno = EINVAL;
            return NULL;
        }
    }

    for (uint32_t attempt = 0U; attempt < 256U; ++attempt) {
        uint32_t serial = __atomic_add_fetch(&sequence, 1U, __ATOMIC_RELAXED);
        uint32_t state = serial * UINT32_C(0x9E3779B9) ^
                         (uint32_t)getpid() * UINT32_C(0x85EBCA6B) ^ attempt;
        for (size_t i = 0; i < 6U; ++i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            suffix[i] = alphabet[state % (sizeof(alphabet) - 1U)];
        }

        if (mkdir(template_path, 0700) == 0) return template_path;
        if (errno != EEXIST) return NULL;
    }

    errno = EEXIST;
    return NULL;
}

/* In-place heapsort keeps qsort allocation-free and gives a bounded
 * O(n log n) comparison count for large upstream-library tables. */
static void swap_elements(unsigned char *left, unsigned char *right,
                          size_t element_size) {
    for (size_t offset = 0; offset < element_size; ++offset) {
        unsigned char temporary = left[offset];
        left[offset] = right[offset];
        right[offset] = temporary;
    }
}

static void sift_down(unsigned char *base, size_t root, size_t count,
                      size_t element_size,
                      int (*compare)(const void *, const void *)) {
    while (root < count / 2U) {
        size_t child = root * 2U + 1U;
        if (child + 1U < count &&
            compare(base + child * element_size,
                    base + (child + 1U) * element_size) < 0)
            ++child;
        if (compare(base + root * element_size,
                    base + child * element_size) >= 0)
            return;
        swap_elements(base + root * element_size,
                      base + child * element_size, element_size);
        root = child;
    }
}

void qsort(void *array, size_t element_count, size_t element_size,
           int (*compare)(const void *, const void *)) {
    if (!array || !compare || element_size == 0U || element_count < 2U ||
        element_count > SIZE_MAX / element_size)
        return;

    unsigned char *base = (unsigned char *)array;
    for (size_t start = element_count / 2U; start > 0U;) {
        --start;
        sift_down(base, start, element_count, element_size, compare);
    }
    for (size_t end = element_count; end > 1U;) {
        --end;
        swap_elements(base, base + end * element_size, element_size);
        sift_down(base, 0U, end, element_size, compare);
    }
}

void *bsearch(const void *key, const void *array, size_t element_count,
              size_t element_size,
              int (*compare)(const void *, const void *)) {
    if (!key || !array || !compare || element_size == 0U ||
        element_count > SIZE_MAX / element_size)
        return NULL;
    const unsigned char *base = (const unsigned char *)array;
    size_t first = 0U;
    size_t count = element_count;
    while (count != 0U) {
        size_t step = count / 2U;
        size_t index = first + step;
        const unsigned char *element = base + index * element_size;
        int order = compare(key, element);
        if (order == 0) return (void *)element;
        if (order > 0) {
            first = index + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return NULL;
}
