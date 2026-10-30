/* Minimal equivalent of the per-DSO C++ destructor hook normally supplied by
 * crtbeginS. Link this into C++ shared objects so dlclose runs only the
 * destructors registered for that image before unmapping it. */
extern void *__dso_handle __attribute__((visibility("hidden")));
extern void __cxa_finalize(void *dso);

static void akaos_cxx_finalize_dso(void) __attribute__((destructor));

static void akaos_cxx_finalize_dso(void) {
    __cxa_finalize(&__dso_handle);
}
