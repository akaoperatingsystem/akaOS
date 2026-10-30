#ifndef XSCORE_RANDOM_H
#define XSCORE_RANDOM_H

#include <stddef.h>
#include <stdint.h>

/* Fill bytes from the architecture's hardware entropy source when available. */
int64_t xscore_random_read(void *buffer, size_t length);

#endif
