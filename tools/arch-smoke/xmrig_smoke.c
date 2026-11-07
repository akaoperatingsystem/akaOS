#include <shim/linux_xmrig.h>
#include <akaos/architecture.h>
#include <stdint.h>

volatile uint32_t xmrig_smoke_arch_id = AKAOS_ARCH_ID;

int akaos_arch_smoke_main(void) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (!CPU_ISSET(0, &cpuset)) return 1;
    CPU_CLR(0, &cpuset);
    if (CPU_ISSET(0, &cpuset)) return 2;
    return (int)xmrig_smoke_arch_id;
}
