#ifndef AKAOS_LIBC_CTYPE_H
#define AKAOS_LIBC_CTYPE_H

#include <locale.h>

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int character);
int isalpha(int character);
int isblank(int character);
int iscntrl(int character);
int isdigit(int character);
int isgraph(int character);
int islower(int character);
int isprint(int character);
int ispunct(int character);
int isspace(int character);
int isupper(int character);
int isxdigit(int character);
int tolower(int character);
int toupper(int character);
/* The classic seven-bit classification helpers that upstream X11 sources
 * still call directly. */
int isascii(int character);
int toascii(int character);
int isalnum_l(int character, locale_t locale);
int isalpha_l(int character, locale_t locale);
int isblank_l(int character, locale_t locale);
int iscntrl_l(int character, locale_t locale);
int isdigit_l(int character, locale_t locale);
int isgraph_l(int character, locale_t locale);
int islower_l(int character, locale_t locale);
int isprint_l(int character, locale_t locale);
int ispunct_l(int character, locale_t locale);
int isspace_l(int character, locale_t locale);
int isupper_l(int character, locale_t locale);
int isxdigit_l(int character, locale_t locale);
int tolower_l(int character, locale_t locale);
int toupper_l(int character, locale_t locale);

const uint16_t **__ctype_b_loc(void);
const int32_t **__ctype_tolower_loc(void);
const int32_t **__ctype_toupper_loc(void);

#ifdef __cplusplus
}
#endif

#endif
