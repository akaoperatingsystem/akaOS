#ifndef AKAOS_LIBC_FEATURES_H
#define AKAOS_LIBC_FEATURES_H

/* akaOS always provides the POSIX and X/Open interfaces, including their
 * thread-safe variants.  The feature macros that upstream sources test for
 * are therefore satisfied unconditionally.
 *
 * _FILE_OFFSET_BITS is deliberately not defined here: the native off_t width
 * follows the target ABI, so the value has to be chosen per port. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS 1
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif

#ifndef _SVID_SOURCE
#define _SVID_SOURCE 1
#endif

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE 1
#endif

#endif
