#ifndef AKAOS_LIBC_RESOLV_H
#define AKAOS_LIBC_RESOLV_H

#include <arpa/nameser.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

int res_query(const char *domain_name, int query_class, int query_type,
              unsigned char *answer, int answer_length);

#ifdef __cplusplus
}
#endif

#endif
