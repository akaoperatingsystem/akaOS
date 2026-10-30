#ifndef AKAOS_LIBC_SYS_PARAM_H
#define AKAOS_LIBC_SYS_PARAM_H

/* akaOS currently has no symbolic-link vnode support. Keep the path-walk
 * guard available to portable Unix code while bounding it to a small limit. */
#ifndef MAXSYMLINKS
#define MAXSYMLINKS 8
#endif

#endif
