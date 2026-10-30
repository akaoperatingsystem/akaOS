#include <akaos/architecture.h>
#include <stdint.h>

/*
 * This deliberately tiny freestanding program is a portability gate, not a
 * second kernel. It proves that the shared SDK headers and a C/assembly entry
 * point can compile and link for each target without a host libc.
 */
volatile uint32_t akaos_arch_smoke_id = AKAOS_ARCH_ID;
volatile uint32_t akaos_arch_smoke_bits = AKAOS_ARCH_BITS;

int akaos_arch_smoke_main(void) {
    return (int)(akaos_arch_smoke_id + akaos_arch_smoke_bits);
}
