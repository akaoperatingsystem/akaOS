/* ============================================================
 * akaOS — String Utilities Implementation
 * ============================================================ */

#include "string.h"

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

void *memset(void *ptr, int value, size_t num) {
    unsigned char *p = (unsigned char *)ptr;
    while (num--)
        *p++ = (unsigned char)value;
    return ptr;
}

void *memcpy(void *dest, const void *src, size_t num) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (num--)
        *d++ = *s++;
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strchr(const char *str, int c) {
    while (*str) {
        if (*str == (char)c)
            return (char *)str;
        str++;
    }
    return (c == '\0') ? (char *)str : 0;
}

int starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++)
            return 0;
    }
    return 1;
}

void int_to_str(int num, char *buf) {
    int i = 0;
    int is_negative = 0;
    char temp[12];

    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    while (num > 0) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }

    if (is_negative)
        temp[i++] = '-';

    int j = 0;
    while (i > 0)
        buf[j++] = temp[--i];
    buf[j] = '\0';
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    while (n--) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return 0;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- > 0 && *src) *d++ = *src++;
    *d = '\0';
    return dest;
}

char *strrchr(const char *str, int c) {
    const char *last = 0;
    while (*str) {
        if (*str == (char)c) last = str;
        str++;
    }
    if (c == '\0') return (char *)str;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return 0;
}

/* strdup uses malloc from all_libc.c */
extern void *malloc(size_t size);
char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

static char *strtok_save = 0;
char *strtok(char *str, const char *delim) {
    if (str) strtok_save = str;
    if (!strtok_save) return 0;
    /* Skip delimiters */
    while (*strtok_save && strchr(delim, *strtok_save)) strtok_save++;
    if (!*strtok_save) return 0;
    char *start = strtok_save;
    while (*strtok_save && !strchr(delim, *strtok_save)) strtok_save++;
    if (*strtok_save) *strtok_save++ = '\0';
    return start;
}

