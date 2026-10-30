/* Initial-exec/static-TLS DSO used to validate the native ABI TLS area.
 * This deliberately has no libc dependency so it can run on every guest
 * architecture with the same loader test. */
__thread int akaos_static_tls_value = 55;
__thread char akaos_static_tls_padding[8192]
    __attribute__((aligned(64), used));

int akaos_static_tls_read(void) {
    return akaos_static_tls_value;
}

void akaos_static_tls_write(int value) {
    akaos_static_tls_value = value;
}

void *akaos_static_tls_alignment_address(void) {
    return akaos_static_tls_padding;
}
