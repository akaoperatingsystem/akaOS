#ifndef AKAOS_LIBC_LANGINFO_H
#define AKAOS_LIBC_LANGINFO_H

#include <locale.h>

typedef enum {
    CODESET = 0,
    D_T_FMT,
    D_FMT,
    T_FMT,
    T_FMT_AMPM,
    AM_STR,
    PM_STR,
    RADIXCHAR,
    THOUSEP,
    YESEXPR,
    NOEXPR
} nl_item;

#ifdef __cplusplus
extern "C" {
#endif
char *nl_langinfo(nl_item item);
char *nl_langinfo_l(nl_item item, locale_t locale);
#ifdef __cplusplus
}
#endif

#endif
