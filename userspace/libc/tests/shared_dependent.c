/* Small dependency-bearing shared object used to exercise the loader's
 * DT_NEEDED namespace.  Providers are resolved from the demo and versioned
 * compatibility DSOs. */
extern int akaos_shared_answer(void);

/* Keep a second entry point bound to a non-default symbol version.  This
 * makes the dependent DSO carry a real DT_VERNEED record so relocation-time
 * version selection is tested independently of dlvsym(). */
extern int akaos_versioned_answer(void);
__asm__(".symver akaos_versioned_answer,akaos_versioned_answer@AKAOS_1.0");

int akaos_dependent_answer(void) {
    return akaos_shared_answer();
}

int akaos_versioned_dependent_answer(void) {
    return akaos_versioned_answer();
}
