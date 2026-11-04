#ifndef AKAOS_LIBC_ICONV_H
#define AKAOS_LIBC_ICONV_H

#include <stddef.h>

typedef void *iconv_t;

#ifdef __cplusplus
extern "C" {
#endif

iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft);
int iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* AKAOS_LIBC_ICONV_H */
