/*
 * The compatibility library that the upstream builds find under the name of
 * the thread library.
 *
 * The upstream desktop programs ask for the thread library in their own
 * configure check, so the name has to exist in the link path.  A program
 * links its libraries in the order of its own list, and the first library
 * that answers for an entry point wins, so a compatibility surface under
 * this name would take the place of the real GLib of every upstream desktop
 * program.  The thread library therefore carries the marker of the runtime
 * ABI and nothing else.
 *
 * The thread entry points themselves come from the C library of akaOS, which
 * implements them.
 */
#include <stdint.h>

#ifndef AKAOS_RUNTIME_ID
#define AKAOS_RUNTIME_ID 1U
#endif

uint32_t akaos_runtime_abi(void) {
    return 2U;
}

uint32_t akaos_runtime_family(void) {
    return AKAOS_RUNTIME_ID;
}
