#include <stdio.h>

extern "C" {
void *__dso_handle __attribute__((visibility("hidden"))) = &__dso_handle;
}

static volatile unsigned constructor_count;

struct SharedCxxProbe {
    SharedCxxProbe() {
        __atomic_add_fetch(&constructor_count, 1U, __ATOMIC_RELEASE);
        puts("C++ DSO global constructor ran");
    }

    ~SharedCxxProbe() {
        puts("C++ DSO global destructor ran");
    }
};

static SharedCxxProbe shared_cxx_probe;

extern "C" int akaos_cxx_dso_constructor_ran(void) {
    return __atomic_load_n(&constructor_count, __ATOMIC_ACQUIRE) == 1U;
}
