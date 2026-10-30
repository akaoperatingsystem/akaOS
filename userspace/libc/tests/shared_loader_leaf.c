/* Tiny uniquely named providers used to exercise a realistic multi-DSO
 * DT_NEEDED list without depending on symbol interposition between fixtures. */
#ifndef AKAOS_LOADER_DEPENDENCY_INDEX
#error AKAOS_LOADER_DEPENDENCY_INDEX must be set by the userspace builder
#endif

#define AKAOS_LOADER_CAT_INNER(prefix, index) prefix##index
#define AKAOS_LOADER_CAT(prefix, index) \
    AKAOS_LOADER_CAT_INNER(prefix, index)

int AKAOS_LOADER_CAT(akaos_loader_dependency_,
                     AKAOS_LOADER_DEPENDENCY_INDEX)(void) {
    return AKAOS_LOADER_DEPENDENCY_INDEX + 1;
}
