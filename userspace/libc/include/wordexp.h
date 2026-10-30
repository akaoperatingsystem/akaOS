#ifndef AKAOS_LIBC_WORDEXP_H
#define AKAOS_LIBC_WORDEXP_H

#include <stddef.h>

enum {
    WRDE_DOOFFS = 1 << 0,
    WRDE_APPEND = 1 << 1,
    WRDE_NOCMD = 1 << 2,
    WRDE_REUSE = 1 << 3,
    WRDE_SHOWERR = 1 << 4,
    WRDE_UNDEF = 1 << 5
};

typedef struct {
    size_t we_wordc;
    char **we_wordv;
    size_t we_offs;
} wordexp_t;

enum {
    WRDE_NOSPACE = 1,
    WRDE_BADCHAR = 2,
    WRDE_BADVAL = 3,
    WRDE_CMDSUB = 4,
    WRDE_SYNTAX = 5
};

#ifdef __cplusplus
extern "C" {
#endif

int wordexp(const char *words, wordexp_t *word_result, int flags);
void wordfree(wordexp_t *word_result);

#ifdef __cplusplus
}
#endif

#endif
