#include <errno.h>
#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

struct block_header {
    size_t size;                /* Size of payload */
    int is_free;                /* 1 if free, 0 if allocated */
    struct block_header *next;  /* Next block in heap */
    struct block_header *prev;  /* Previous block in heap */
    struct block_header *free_next;
    struct block_header *free_prev;
    void *user_ptr;             /* Returned pointer (may be over-aligned) */
};

#define BLOCK_HEADER_SIZE ALIGN(sizeof(struct block_header) + sizeof(struct block_header *))
_Static_assert(BLOCK_HEADER_SIZE >= sizeof(struct block_header) + sizeof(struct block_header *),
               "heap header must reserve a user-pointer back-reference");

static struct block_header *heap_head = NULL;
static struct block_header *heap_tail = NULL;
static struct block_header *heap_free_head = NULL;
static uint32_t heap_lock_state;
static pthread_once_t heap_atfork_once = PTHREAD_ONCE_INIT;

static void heap_lock_acquire(void) {
    while (__atomic_exchange_n(&heap_lock_state, 1U, __ATOMIC_ACQUIRE) != 0U) {
        while (__atomic_load_n(&heap_lock_state, __ATOMIC_RELAXED) != 0U)
            __asm__ volatile("" ::: "memory");
    }
}

static void heap_lock_release(void) {
    __atomic_store_n(&heap_lock_state, 0U, __ATOMIC_RELEASE);
}

static void heap_fork_prepare(void) {
    heap_lock_acquire();
}

static void heap_fork_parent(void) {
    heap_lock_release();
}

static void heap_fork_child(void) {
    /* Only the forking thread survives; a vanished sibling may have owned the
     * copied allocator lock at the instant XSCore cloned the address space. */
    __atomic_store_n(&heap_lock_state, 0U, __ATOMIC_RELEASE);
}

static void heap_register_atfork(void) {
    if (pthread_atfork(heap_fork_prepare, heap_fork_parent,
                       heap_fork_child) != 0)
        abort();
}

static void heap_initialize(void) {
    if (pthread_once(&heap_atfork_once, heap_register_atfork) != 0) abort();
}

static int align_allocation_size(size_t size, size_t *aligned_size) {
    if (size > SIZE_MAX - (ALIGNMENT - 1U)) {
        errno = ENOMEM;
        return -1;
    }
    *aligned_size = ALIGN(size);
    return 0;
}

static void free_list_remove(struct block_header *block) {
    if (block->free_prev) {
        block->free_prev->free_next = block->free_next;
    } else if (heap_free_head == block) {
        heap_free_head = block->free_next;
    }
    if (block->free_next) {
        block->free_next->free_prev = block->free_prev;
    }
    block->free_next = NULL;
    block->free_prev = NULL;
}

static void free_list_insert(struct block_header *block) {
    block->free_prev = NULL;
    block->free_next = heap_free_head;
    if (heap_free_head) {
        heap_free_head->free_prev = block;
    }
    heap_free_head = block;
}

static void set_user_pointer(struct block_header *block, void *user_ptr) {
    block->user_ptr = user_ptr;
    struct block_header **back_reference =
        (struct block_header **)((uint8_t *)user_ptr - sizeof(struct block_header *));
    *back_reference = block;
}

/* Caller holds heap_lock_state.  Keeping the list mutation in one locked
 * region makes malloc/free/realloc safe when desktop libraries use pthreads. */
static void *allocate_locked(size_t aligned_size) {
    /* Search only free blocks, not every allocation in the heap. */
    struct block_header *curr = heap_free_head;

    while (curr && curr->size < aligned_size) {
        curr = curr->free_next;
    }

    if (curr) {
        free_list_remove(curr);

        /* Can we split this block? */
        if (curr->size - aligned_size >= BLOCK_HEADER_SIZE + ALIGNMENT) {
            size_t remaining = curr->size - aligned_size - BLOCK_HEADER_SIZE;
            struct block_header *new_block =
                (struct block_header *)(void *)((uint8_t *)curr + BLOCK_HEADER_SIZE + aligned_size);
            new_block->size = remaining;
            new_block->is_free = 1;
            new_block->next = curr->next;
            new_block->prev = curr;
            new_block->free_next = NULL;
            new_block->free_prev = NULL;
            new_block->user_ptr = NULL;
            if (curr->next) {
                curr->next->prev = new_block;
            } else {
                heap_tail = new_block;
            }
            curr->next = new_block;
            curr->size = aligned_size;
            free_list_insert(new_block);
        }

        curr->is_free = 0;
        set_user_pointer(curr, (void *)((uint8_t *)curr + BLOCK_HEADER_SIZE));
        return curr->user_ptr;
    }

    /* Need to expand heap */
    if (aligned_size > SIZE_MAX - BLOCK_HEADER_SIZE ||
        aligned_size + BLOCK_HEADER_SIZE > (size_t)INTPTR_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total_alloc = aligned_size + BLOCK_HEADER_SIZE;
    size_t request_size = total_alloc < 4096 ? 4096 : total_alloc;
    if (request_size > (size_t)INTPTR_MAX) {
        errno = ENOMEM;
        return NULL;
    }

    void *mem = sbrk((intptr_t)request_size);
    if (mem == (void *)-1) {
        size_t mmap_size = (request_size + 4095) & ~(size_t)4095;
        mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            errno = ENOMEM;
            return NULL;
        }
        request_size = mmap_size;
    }

    struct block_header *new_block = (struct block_header *)mem;
    new_block->size = request_size - BLOCK_HEADER_SIZE;
    new_block->is_free = 0;
    new_block->next = NULL;
    new_block->prev = heap_tail;
    new_block->free_next = NULL;
    new_block->free_prev = NULL;
    new_block->user_ptr = NULL;

    if (heap_tail) {
        heap_tail->next = new_block;
    } else {
        heap_head = new_block;
    }
    heap_tail = new_block;

    /* If we allocated more than requested, split it */
    if (new_block->size - aligned_size >= BLOCK_HEADER_SIZE + ALIGNMENT) {
        size_t remaining = new_block->size - aligned_size - BLOCK_HEADER_SIZE;
        struct block_header *split_block = (struct block_header *)(void *)((uint8_t *)new_block + BLOCK_HEADER_SIZE + aligned_size);
        split_block->size = remaining;
        split_block->is_free = 1;
        split_block->next = NULL;
        split_block->prev = new_block;
        split_block->free_next = NULL;
        split_block->free_prev = NULL;
        split_block->user_ptr = NULL;
        new_block->next = split_block;
        heap_tail = split_block;
        new_block->size = aligned_size;
        free_list_insert(split_block);
    }

    set_user_pointer(new_block, (void *)((uint8_t *)new_block + BLOCK_HEADER_SIZE));
    return new_block->user_ptr;
}

/* Caller holds heap_lock_state. */
static struct block_header *find_allocated_header_locked(const void *ptr) {
    if (!ptr || !heap_head || !heap_tail) return NULL;

    uintptr_t heap_begin = (uintptr_t)heap_head;
    uintptr_t heap_end = (uintptr_t)heap_tail + BLOCK_HEADER_SIZE + heap_tail->size;
    uintptr_t user_address = (uintptr_t)ptr;
    if (user_address < heap_begin + BLOCK_HEADER_SIZE || user_address >= heap_end)
        return NULL;

    uintptr_t back_reference_address = user_address - sizeof(struct block_header *);
    if (back_reference_address < heap_begin ||
        back_reference_address > heap_end - sizeof(struct block_header *))
        return NULL;

    struct block_header *block =
        *(struct block_header **)(void *)back_reference_address;
    uintptr_t block_address = (uintptr_t)block;
    if (!block || block_address < heap_begin ||
        block_address > heap_end - BLOCK_HEADER_SIZE ||
        (block_address & (ALIGNMENT - 1U)) != 0U)
        return NULL;
    if (block->is_free || block->user_ptr != ptr) return NULL;
    return block;
}

static void free_locked(void *ptr) {
    struct block_header *header = find_allocated_header_locked(ptr);
    if (!header) return;
    header->is_free = 1;
    header->user_ptr = NULL;
    *(struct block_header **)((uint8_t *)ptr - sizeof(struct block_header *)) = NULL;

    /* Coalesce with next if free */
    if (header->next && header->next->is_free) {
        struct block_header *next = header->next;
        free_list_remove(next);
        header->size += BLOCK_HEADER_SIZE + header->next->size;
        header->next = next->next;
        if (header->next) {
            header->next->prev = header;
        } else {
            heap_tail = header;
        }
    }

    /* Coalesce with prev if free */
    if (header->prev && header->prev->is_free) {
        struct block_header *prev = header->prev;
        free_list_remove(prev);
        prev->size += BLOCK_HEADER_SIZE + header->size;
        prev->next = header->next;
        if (header->next) {
            header->next->prev = prev;
        } else {
            heap_tail = prev;
        }
        header = prev;
    }

    free_list_insert(header);
}

void *malloc(size_t size) {
    if (size == 0U) return NULL;
    size_t aligned_size;
    if (align_allocation_size(size, &aligned_size) != 0) return NULL;
    heap_initialize();
    heap_lock_acquire();
    void *memory = allocate_locked(aligned_size);
    heap_lock_release();
    return memory;
}

void free(void *ptr) {
    if (!ptr) return;
    heap_initialize();
    heap_lock_acquire();
    free_locked(ptr);
    heap_lock_release();
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    size_t aligned_size;
    if (align_allocation_size(size, &aligned_size) != 0) return NULL;
    heap_initialize();
    heap_lock_acquire();
    struct block_header *header = find_allocated_header_locked(ptr);
    if (!header) {
        errno = EINVAL;
        heap_lock_release();
        return NULL;
    }

    if (header->size >= aligned_size) {
        heap_lock_release();
        return ptr;
    }

    /* Try to merge with next block if free */
    int merge_next = header->next && header->next->is_free &&
        header->size <= SIZE_MAX - BLOCK_HEADER_SIZE &&
        header->next->size <= SIZE_MAX - header->size - BLOCK_HEADER_SIZE &&
        header->size + BLOCK_HEADER_SIZE + header->next->size >= aligned_size;
    if (merge_next) {
        struct block_header *next = header->next;
        free_list_remove(next);
        header->size += BLOCK_HEADER_SIZE + next->size;
        header->next = next->next;
        if (header->next) {
            header->next->prev = header;
        } else {
            heap_tail = header;
        }
        heap_lock_release();
        return ptr;
    }

    void *new_ptr = allocate_locked(aligned_size);
    if (!new_ptr) {
        heap_lock_release();
        return NULL;
    }

    size_t copy_size = header->size < size ? header->size : size;
    memcpy(new_ptr, ptr, copy_size);
    free_locked(ptr);
    heap_lock_release();
    return new_ptr;
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
        size % alignment != 0U) {
        errno = EINVAL;
        return NULL;
    }
    if (alignment <= ALIGNMENT) return malloc(size);
    size_t overhead = alignment - 1U;
    size_t back_reference_size = sizeof(struct block_header *);
    if (overhead > SIZE_MAX - back_reference_size ||
        size > SIZE_MAX - overhead - back_reference_size) {
        errno = ENOMEM;
        return NULL;
    }

    size_t allocation_size = size + overhead + back_reference_size;
    void *base = malloc(allocation_size);
    if (!base) return NULL;
    uintptr_t base_address = (uintptr_t)base;
    if (base_address > UINTPTR_MAX - allocation_size) {
        free(base);
        errno = ENOMEM;
        return NULL;
    }
    uintptr_t minimum_address = base_address + back_reference_size;
    if (minimum_address > UINTPTR_MAX - overhead) {
        free(base);
        errno = ENOMEM;
        return NULL;
    }
    uintptr_t aligned_address =
        (minimum_address + overhead) & ~((uintptr_t)alignment - 1U);

    heap_lock_acquire();
    struct block_header *header = find_allocated_header_locked(base);
    if (header) {
        header->user_ptr = (void *)aligned_address;
        *(struct block_header **)(void *)(aligned_address - back_reference_size) = header;
    }
    heap_lock_release();
    if (!header) {
        free(base);
        errno = EINVAL;
        return NULL;
    }
    return (void *)aligned_address;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr || alignment < sizeof(void *) ||
        (alignment & (alignment - 1U)) != 0U)
        return EINVAL;

    size_t padded_size = size;
    size_t remainder = padded_size % alignment;
    if (remainder != 0U) {
        size_t padding = alignment - remainder;
        if (padded_size > SIZE_MAX - padding) return ENOMEM;
        padded_size += padding;
    }

    int saved_errno = errno;
    void *allocation = aligned_alloc(alignment, padded_size);
    int allocation_error = errno;
    errno = saved_errno;
    if (!allocation) return allocation_error ? allocation_error : ENOMEM;
    *memptr = allocation;
    return 0;
}

int atoi(const char *nptr) {
    if (!nptr) return 0;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') ++nptr;
    int sign = 1;
    if (*nptr == '-') {
        sign = -1;
        ++nptr;
    } else if (*nptr == '+') {
        ++nptr;
    }
    int result = 0;
    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        ++nptr;
    }
    return sign * result;
}

int abs(int value) {
    return value < 0 ? -value : value;
}

long labs(long value) {
    return value < 0 ? -value : value;
}

long long llabs(long long value) {
    return value < 0 ? -value : value;
}

intmax_t imaxabs(intmax_t value) {
    return value < 0 ? -value : value;
}

div_t div(int numerator, int denominator) {
    div_t result = { numerator / denominator, numerator % denominator };
    return result;
}

ldiv_t ldiv(long numerator, long denominator) {
    ldiv_t result = { numerator / denominator, numerator % denominator };
    return result;
}

lldiv_t lldiv(long long numerator, long long denominator) {
    lldiv_t result = { numerator / denominator, numerator % denominator };
    return result;
}

long atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

long long atoll(const char *nptr) {
    return strtoll(nptr, NULL, 10);
}

long strtol(const char *nptr, char **endptr, int base) {
    if (!nptr) {
        if (endptr) *endptr = NULL;
        return 0;
    }
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') ++nptr;
    int sign = 1;
    if (*nptr == '-') {
        sign = -1;
        ++nptr;
    } else if (*nptr == '+') {
        ++nptr;
    }
    if (base == 0) {
        if (*nptr == '0') {
            if (nptr[1] == 'x' || nptr[1] == 'X') {
                base = 16;
                nptr += 2;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        nptr += 2;
    }
    long result = 0;
    const char *start = nptr;
    while (*nptr) {
        int digit = -1;
        if (*nptr >= '0' && *nptr <= '9') digit = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'z') digit = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'Z') digit = *nptr - 'A' + 10;
        if (digit < 0 || digit >= base) break;
        result = result * base + digit;
        ++nptr;
    }
    if (endptr) {
        *endptr = (char *)(nptr == start ? (sign == -1 ? start - 1 : start) : nptr);
    }
    return sign * result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

static int ascii_digit_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'z') return character - 'a' + 10;
    if (character >= 'A' && character <= 'Z') return character - 'A' + 10;
    return -1;
}

static unsigned long long parse_unsigned_integer(const char *text,
                                                  char **endptr,
                                                  int base,
                                                  int *negative,
                                                  int *overflow) {
    const char *original = text;
    while (*text == ' ' || *text == '\t' || *text == '\n' ||
           *text == '\r' || *text == '\v' || *text == '\f') ++text;
    *negative = 0;
    if (*text == '-') { *negative = 1; ++text; }
    else if (*text == '+') ++text;

    if (base != 0 && (base < 2 || base > 36)) {
        errno = EINVAL;
        if (endptr) *endptr = (char *)original;
        return 0;
    }
    if ((base == 0 || base == 16) && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X') &&
        ascii_digit_value(text[2]) >= 0 && ascii_digit_value(text[2]) < 16) {
        base = 16;
        text += 2;
    } else if (base == 0) {
        base = text[0] == '0' ? 8 : 10;
    }

    const char *digits_start = text;
    unsigned long long value = 0;
    *overflow = 0;
    while (*text) {
        int digit = ascii_digit_value(*text);
        if (digit < 0 || digit >= base) break;
        if (value > (ULLONG_MAX - (unsigned long long)digit) /
                    (unsigned long long)base) {
            *overflow = 1;
            value = ULLONG_MAX;
        } else if (!*overflow) {
            value = value * (unsigned long long)base +
                    (unsigned long long)digit;
        }
        ++text;
    }
    if (text == digits_start) {
        if (endptr) *endptr = (char *)original;
        return 0;
    }
    if (endptr) *endptr = (char *)text;
    return value;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    int negative = 0, overflow = 0;
    unsigned long long magnitude = parse_unsigned_integer(
        nptr, endptr, base, &negative, &overflow);
    unsigned long long limit = negative ?
        (unsigned long long)LLONG_MAX + 1ULL : (unsigned long long)LLONG_MAX;
    if (overflow || magnitude > limit) {
        errno = ERANGE;
        return negative ? LLONG_MIN : LLONG_MAX;
    }
    if (negative) {
        if (magnitude == (unsigned long long)LLONG_MAX + 1ULL) return LLONG_MIN;
        return -(long long)magnitude;
    }
    return (long long)magnitude;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    int negative = 0, overflow = 0;
    unsigned long long magnitude = parse_unsigned_integer(
        nptr, endptr, base, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return negative ? 0ULL - magnitude : magnitude;
}

long long strtoll_l(const char *nptr, char **endptr, int base,
                    locale_t locale) {
    (void)locale;
    return strtoll(nptr, endptr, base);
}

unsigned long long strtoull_l(const char *nptr, char **endptr, int base,
                              locale_t locale) {
    (void)locale;
    return strtoull(nptr, endptr, base);
}

static int ascii_case_equal_prefix(const char *text, const char *word) {
    while (*word) {
        char left = *text++;
        char right = *word++;
        if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
        if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
        if (left != right) return 0;
    }
    return 1;
}

static long double parse_floating(const char *nptr, char **endptr) {
    const char *original = nptr;
    const char *text = nptr;
    while (*text == ' ' || *text == '\t' || *text == '\n' ||
           *text == '\r' || *text == '\v' || *text == '\f') ++text;
    int negative = 0;
    if (*text == '-') { negative = 1; ++text; }
    else if (*text == '+') ++text;

    const char *special_start = text;
    if (ascii_case_equal_prefix(text, "infinity")) text += 8;
    else if (ascii_case_equal_prefix(text, "inf")) text += 3;
    else if (ascii_case_equal_prefix(text, "nan")) {
        text += 3;
        if (*text == '(') {
            const char *close = text + 1;
            while ((*close >= '0' && *close <= '9') ||
                   (*close >= 'a' && *close <= 'z') ||
                   (*close >= 'A' && *close <= 'Z') || *close == '_') ++close;
            if (*close == ')') text = close + 1;
        }
        if (endptr) *endptr = (char *)text;
        long double result = __builtin_nanl("");
        return negative ? -result : result;
    }
    if (text != special_start) {
        if (endptr) *endptr = (char *)text;
        long double result = __builtin_infl();
        return negative ? -result : result;
    }

    int hexadecimal = text[0] == '0' &&
                      (text[1] == 'x' || text[1] == 'X');
    if (hexadecimal) text += 2;
    int radix = hexadecimal ? 16 : 10;
    long double significand = 0.0L;
    int exponent_adjust = 0;
    int kept_digits = 0;
    int significant_started = 0;
    int decimal_seen = 0;
    int have_digit = 0;
    for (;;) {
        int digit = ascii_digit_value(*text);
        if (*text == '.' && !decimal_seen) {
            decimal_seen = 1;
            ++text;
            continue;
        }
        if (digit < 0 || digit >= radix) break;
        have_digit = 1;
        if (!significant_started && digit != 0) significant_started = 1;
        if (!significant_started) {
            if (decimal_seen) --exponent_adjust;
        } else if (kept_digits < 36) {
            significand = significand * (long double)radix + (long double)digit;
            ++kept_digits;
            if (decimal_seen) --exponent_adjust;
        } else if (!decimal_seen) {
            ++exponent_adjust;
        }
        ++text;
    }
    if (!have_digit) {
        if (endptr) *endptr = (char *)original;
        return 0.0L;
    }

    int exponent_marker = hexadecimal ? (*text == 'p' || *text == 'P') :
                                        (*text == 'e' || *text == 'E');
    if (exponent_marker) {
        const char *marker = text++;
        int exponent_negative = 0;
        if (*text == '-') { exponent_negative = 1; ++text; }
        else if (*text == '+') ++text;
        if (*text >= '0' && *text <= '9') {
            int explicit_exponent = 0;
            while (*text >= '0' && *text <= '9') {
                int digit = *text++ - '0';
                if (explicit_exponent < 100000)
                    explicit_exponent = explicit_exponent * 10 + digit;
            }
            exponent_adjust += exponent_negative ? -explicit_exponent :
                                                   explicit_exponent;
        } else {
            text = marker;
        }
    }
    if (endptr) *endptr = (char *)text;

    if (significand == 0.0L) return negative ? -0.0L : 0.0L;
    int scale_base = hexadecimal ? 2 : 10;
    if (exponent_adjust > 5000) {
        errno = ERANGE;
        return negative ? -__builtin_infl() : __builtin_infl();
    }
    if (exponent_adjust < -5000) {
        errno = ERANGE;
        return negative ? -0.0L : 0.0L;
    }
    unsigned int power = (unsigned int)(exponent_adjust < 0 ?
                                           -exponent_adjust : exponent_adjust);
    long double factor = (long double)scale_base;
    long double scale = 1.0L;
    while (power != 0U) {
        if (power & 1U) scale *= factor;
        power >>= 1U;
        if (power != 0U) factor *= factor;
    }
    if (exponent_adjust < 0) scale = 1.0L / scale;
    long double result = significand * scale;
    if (__builtin_isinf(result) || result == 0.0L) errno = ERANGE;
    return negative ? -result : result;
}

long double strtold(const char *nptr, char **endptr) {
    return parse_floating(nptr, endptr);
}

double strtod(const char *nptr, char **endptr) {
    long double parsed = parse_floating(nptr, endptr);
    double result = (double)parsed;
    if (__builtin_isinf(result) && !__builtin_isinf(parsed)) errno = ERANGE;
    return result;
}

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

float strtof(const char *nptr, char **endptr) {
    long double parsed = parse_floating(nptr, endptr);
    float result = (float)parsed;
    if (__builtin_isinf(result) && !__builtin_isinf(parsed)) errno = ERANGE;
    return result;
}

float strtof_l(const char *nptr, char **endptr, locale_t locale) {
    (void)locale;
    return strtof(nptr, endptr);
}

double strtod_l(const char *nptr, char **endptr, locale_t locale) {
    (void)locale;
    return strtod(nptr, endptr);
}

long double strtold_l(const char *nptr, char **endptr, locale_t locale) {
    (void)locale;
    return strtold(nptr, endptr);
}

void abort(void) {
    _exit(134);
}

void exit(int status) {
    extern void __cxa_finalize(void *dso);
    __cxa_finalize(NULL);
    _exit(status);
}
