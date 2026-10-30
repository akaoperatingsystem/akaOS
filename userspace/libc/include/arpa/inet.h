#ifndef AKAOS_LIBC_ARPA_INET_H
#define AKAOS_LIBC_ARPA_INET_H

#include <stdint.h>
#include <netinet/in.h>

uint32_t inet_addr(const char *cp);
char *inet_ntoa(struct in_addr in);
int inet_aton(const char *cp, struct in_addr *inp);
int inet_pton(int family, const char *source, void *destination);
const char *inet_ntop(int family, const void *source, char *destination,
                      socklen_t destination_size);

#endif
