#ifndef XSCORE_USERCOPY_H
#define XSCORE_USERCOPY_H

#include <stddef.h>
#include <stdint.h>

int xscore_user_range_prepare(uint64_t address, size_t length, int write);
void *xscore_memcpy(void *destination, const void *source, size_t length);
int xscore_copy_from_user(void *destination, uint64_t source, size_t length);
int xscore_copy_to_user(uint64_t destination, const void *source, size_t length);
int xscore_copy_string_from_user(char *destination, size_t capacity,
                                 uint64_t source, size_t *length_out);

#endif
