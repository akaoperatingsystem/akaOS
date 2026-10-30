#ifndef AKAOS_LIBC_WCTYPE_H
#define AKAOS_LIBC_WCTYPE_H

#include <locale.h>
#include <wchar.h>

typedef unsigned long wctype_t;

#ifdef __cplusplus
extern "C" {
#endif

wctype_t wctype(const char *property);
int iswctype(wint_t character, wctype_t property);
int iswctype_l(wint_t character, wctype_t property, locale_t locale);
int iswblank(wint_t character);
int iswcntrl(wint_t character);
int iswgraph(wint_t character);
int iswlower(wint_t character);
int iswprint(wint_t character);
int iswpunct(wint_t character);
int iswupper(wint_t character);
int iswxdigit(wint_t character);
int iswalnum_l(wint_t character, locale_t locale);
int iswalpha_l(wint_t character, locale_t locale);
int iswblank_l(wint_t character, locale_t locale);
int iswcntrl_l(wint_t character, locale_t locale);
int iswdigit_l(wint_t character, locale_t locale);
int iswgraph_l(wint_t character, locale_t locale);
int iswlower_l(wint_t character, locale_t locale);
int iswprint_l(wint_t character, locale_t locale);
int iswpunct_l(wint_t character, locale_t locale);
int iswspace_l(wint_t character, locale_t locale);
int iswupper_l(wint_t character, locale_t locale);
int iswxdigit_l(wint_t character, locale_t locale);
wint_t towlower_l(wint_t character, locale_t locale);
wint_t towupper_l(wint_t character, locale_t locale);

#ifdef __cplusplus
}
#endif

#endif
