/* Deliberately self-contained: this is linked as a real ET_DYN shared
 * object, so the loader test does not hide missing libc relocations. */
static volatile int constructor_ran;
__thread int akaos_tls_probe_value = 37;

__attribute__((constructor))
static void akaos_demo_constructor(void) {
    constructor_ran = 1;
}

int akaos_shared_answer(void) {
    return 42;
}

int akaos_shared_constructor_ran(void) {
    return constructor_ran == 1;
}

/* Exercise the compiler's real TLS access sequence as well as dlsym of the
 * STT_TLS object.  A desktop DSO normally reaches TLS through
 * __tls_get_addr; keeping these tiny helpers in the test image makes that
 * relocation/ABI contract observable on every supported architecture. */
int akaos_tls_probe_read(void) {
    return akaos_tls_probe_value;
}

void akaos_tls_probe_write(int value) {
    akaos_tls_probe_value = value;
}
