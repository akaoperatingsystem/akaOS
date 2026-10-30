#include <xscore/process.h>
#include <xscore/usercopy.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)

typedef uintptr_t xscore_alias_word __attribute__((__may_alias__));

void *xscore_memcpy(void *destination, const void *source, size_t length) {
    uint8_t *output = destination;
    const uint8_t *input = source;
    const uintptr_t alignment_mask = sizeof(xscore_alias_word) - 1U;

    if ((((uintptr_t)output ^ (uintptr_t)input) & alignment_mask) == 0) {
        while (length != 0 && ((uintptr_t)output & alignment_mask) != 0) {
            *output++ = *input++;
            --length;
        }

        xscore_alias_word *output_words =
            (xscore_alias_word *)(void *)output;
        const xscore_alias_word *input_words =
            (const xscore_alias_word *)(const void *)input;
        while (length >= sizeof(xscore_alias_word)) {
            *output_words++ = *input_words++;
            length -= sizeof(xscore_alias_word);
        }
        output = (uint8_t *)(void *)output_words;
        input = (const uint8_t *)(const void *)input_words;
    }

    while (length != 0) {
        *output++ = *input++;
        --length;
    }
    return destination;
}

int xscore_user_range_prepare(uint64_t address, size_t length, int write) {
    if (length == 0) return 1;
    if (address == 0 || address > UINT64_MAX - (uint64_t)length) return 0;

    uint64_t first = address & ~(PAGE_SIZE - 1);
    uint64_t last = (address + (uint64_t)length - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t page = first;; page += PAGE_SIZE) {
        uint64_t flags = 0;
        int mapped = xscore_vmm_is_mapped(page, &flags);
        if (!mapped || (write && (flags & XSCORE_PAGE_WRITABLE) == 0)) {
            uint32_t fault_flags = write ? XSCORE_USER_FAULT_WRITE : 0;
            if (mapped) fault_flags |= XSCORE_USER_FAULT_PRESENT;
            if (!xscore_process_handle_page_fault(page, fault_flags)) return 0;
            flags = 0;
            mapped = xscore_vmm_is_mapped(page, &flags);
        }
        if (!mapped || (flags & XSCORE_PAGE_USER) == 0 ||
            (write && (flags & XSCORE_PAGE_WRITABLE) == 0)) {
            return 0;
        }
        if (page == last) break;
        if (page > UINT64_MAX - PAGE_SIZE) return 0;
    }
    return 1;
}

int xscore_copy_from_user(void *destination, uint64_t source, size_t length) {
    if (length == 0) return 1;
    if (!destination || !xscore_user_range_prepare(source, length, 0)) return 0;
    xscore_memcpy(destination, (const void *)(uintptr_t)source, length);
    return 1;
}

int xscore_copy_to_user(uint64_t destination, const void *source,
                        size_t length) {
    if (length == 0) return 1;
    if (!source || !xscore_user_range_prepare(destination, length, 1)) return 0;
    xscore_memcpy((void *)(uintptr_t)destination, source, length);
    return 1;
}

int xscore_copy_string_from_user(char *destination, size_t capacity,
                                 uint64_t source, size_t *length_out) {
    if (!destination || capacity == 0 || source == 0) return 0;
    for (size_t i = 0; i < capacity; ++i) {
        char value;
        if (!xscore_copy_from_user(&value, source + i, 1)) return 0;
        destination[i] = value;
        if (value == '\0') {
            if (length_out) *length_out = i;
            return 1;
        }
    }
    destination[capacity - 1] = '\0';
    return 0;
}

void *memcpy(void *destination, const void *source, size_t count) {
    return xscore_memcpy(destination, source, count);
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *dst = destination;
    while (count--) *dst++ = (uint8_t)value;
    return destination;
}
