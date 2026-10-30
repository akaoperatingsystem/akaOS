#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef int (*answer_function)(void);
typedef int (*constructor_status_function)(void);
typedef int (*tls_read_function)(void);
typedef void (*tls_write_function)(int);
typedef void *(*tls_address_function)(void);

struct dl_error_worker_context {
    pthread_barrier_t *barrier;
    volatile uint32_t *start;
};

struct dl_lifecycle_worker_context {
    pthread_barrier_t *barrier;
    volatile uint32_t *start;
    uint32_t iterations;
};

struct phdr_probe_context {
    unsigned int image_count;
    int found_demo_library;
    int found_demo_load_segment;
    int invalid_image;
};

static int inspect_loaded_program_headers(struct dl_phdr_info *info,
                                          size_t info_size, void *argument) {
    struct phdr_probe_context *context =
        (struct phdr_probe_context *)argument;
    if (!context || !info || info_size < sizeof(*info) || !info->dlpi_phdr ||
        info->dlpi_phnum == 0) {
        if (context) context->invalid_image = 1;
        return 1;
    }
    ++context->image_count;
    int has_load_segment = 0;
    for (Elf_Half index = 0; index < info->dlpi_phnum; ++index) {
        if (info->dlpi_phdr[index].p_type == PT_LOAD) {
            has_load_segment = 1;
            break;
        }
    }
    if (info->dlpi_name &&
        strcmp(info->dlpi_name, "/lib/libakaos-demo.so") == 0) {
        context->found_demo_library = 1;
        context->found_demo_load_segment = has_load_segment;
    }
    return 0;
}

static int loader_barrier_wait(pthread_barrier_t *barrier) {
    int result = pthread_barrier_wait(barrier);
    return result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD;
}

static int loader_worker_wait_start(volatile uint32_t *start) {
    uint32_t state;
    do {
        state = __atomic_load_n(start, __ATOMIC_ACQUIRE);
        if (state == 0U) (void)sched_yield();
    } while (state == 0U);
    return state == 1U;
}

static void *dlerror_isolation_worker(void *argument) {
    struct dl_error_worker_context *context =
        (struct dl_error_worker_context *)argument;
    if (!loader_worker_wait_start(context->start)) return NULL;
    int failed = dlopen("", RTLD_NOW | RTLD_LOCAL) != NULL;
    if (!loader_barrier_wait(context->barrier)) failed = 1;
    char *error = dlerror();
    if (!error || strcmp(error, "dlopen: invalid path") != 0) failed = 1;
    return (void *)(uintptr_t)(failed != 0);
}

static void *concurrent_loader_worker(void *argument) {
    struct dl_lifecycle_worker_context *context =
        (struct dl_lifecycle_worker_context *)argument;
    if (!loader_worker_wait_start(context->start)) return NULL;
    int failed = 0;
    for (uint32_t iteration = 0; iteration < context->iterations; ++iteration) {
        void *handle = dlopen("/lib/libakaos-demo.so",
                              RTLD_NOW | RTLD_LOCAL);
        if (!handle) failed = 1;
        if (!loader_barrier_wait(context->barrier)) failed = 1;
        answer_function answer = handle
            ? (answer_function)dlsym(handle, "akaos_shared_answer") : NULL;
        if (!answer || answer() != 42) failed = 1;
        if (!loader_barrier_wait(context->barrier)) failed = 1;
        if (handle && dlclose(handle) != 0) failed = 1;
    }
    return (void *)(uintptr_t)(failed != 0);
}

static int run_dlerror_isolation_test(void) {
    pthread_barrier_t barrier;
    if (pthread_barrier_init(&barrier, NULL, 2U) != 0) return 0;
    volatile uint32_t start = 0U;
    struct dl_error_worker_context context = {&barrier, &start};
    pthread_t workers[2] = {0};
    uint32_t created = 0U;
    for (; created < 2U; ++created) {
        if (pthread_create(&workers[created], NULL, dlerror_isolation_worker,
                           &context) != 0) break;
    }
    __atomic_store_n(&start, created == 2U ? 1U : 2U, __ATOMIC_RELEASE);
    int passed = created == 2U;
    for (uint32_t worker = 0; worker < created; ++worker) {
        void *result = NULL;
        if (pthread_join(workers[worker], &result) != 0 ||
            (uintptr_t)result != 0U) passed = 0;
    }
    (void)pthread_barrier_destroy(&barrier);
    return passed;
}

static int run_concurrent_loader_test(void) {
    enum { WORKER_COUNT = 4U, ITERATIONS = 8U };
    pthread_barrier_t barrier;
    if (pthread_barrier_init(&barrier, NULL, WORKER_COUNT) != 0) return 0;
    volatile uint32_t start = 0U;
    struct dl_lifecycle_worker_context context = {
        &barrier, &start, ITERATIONS
    };
    pthread_t workers[WORKER_COUNT] = {0};
    uint32_t created = 0U;
    for (; created < WORKER_COUNT; ++created) {
        if (pthread_create(&workers[created], NULL, concurrent_loader_worker,
                           &context) != 0) break;
    }
    __atomic_store_n(&start, created == WORKER_COUNT ? 1U : 2U,
                     __ATOMIC_RELEASE);
    int passed = created == WORKER_COUNT;
    for (uint32_t worker = 0; worker < created; ++worker) {
        void *result = NULL;
        if (pthread_join(workers[worker], &result) != 0 ||
            (uintptr_t)result != 0U) passed = 0;
    }
    (void)pthread_barrier_destroy(&barrier);
    return passed;
}

static void *tls_worker(void *argument) {
    void *handle = argument;
    tls_read_function read_value =
        (tls_read_function)dlsym(handle, "akaos_tls_probe_read");
    tls_write_function write_value =
        (tls_write_function)dlsym(handle, "akaos_tls_probe_write");
    int initial = read_value ? read_value() : -1;
    if (!read_value || !write_value || initial != 37) return NULL;
    write_value(84);
    return (void *)(uintptr_t)read_value();
}

static void *static_tls_worker(void *argument) {
    void *handle = argument;
    tls_read_function read_value =
        (tls_read_function)dlsym(handle, "akaos_static_tls_read");
    tls_write_function write_value =
        (tls_write_function)dlsym(handle, "akaos_static_tls_write");
    tls_address_function alignment_address = (tls_address_function)dlsym(
        handle, "akaos_static_tls_alignment_address");
    if (!read_value || !write_value || !alignment_address ||
        read_value() != 55 || ((uintptr_t)alignment_address() & 63U) != 0U)
        return NULL;
    write_value(123);
    return (void *)(uintptr_t)read_value();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    void *not_loaded = dlopen("/lib/libakaos-demo.so",
                              RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (not_loaded) {
        (void)dlclose(not_loaded);
        puts("dltest: RTLD_NOLOAD loaded an absent object");
        return 560;
    }
    char *missing_error = dlerror();
    if (!missing_error ||
        strcmp(missing_error, "dlopen: object is not already loaded") != 0) {
        puts("dltest: RTLD_NOLOAD miss reporting failed");
        return 561;
    }

    void *main_handle = dlopen(NULL, RTLD_NOW);
    void *main_tls_symbol = main_handle
        ? dlsym(main_handle, "__tls_get_addr") : NULL;
    void *default_tls_symbol = dlsym(RTLD_DEFAULT, "__tls_get_addr");
    if (!main_handle || !main_tls_symbol ||
        main_tls_symbol != default_tls_symbol || dlclose(main_handle) != 0) {
        puts("dltest: dlopen(NULL) main-program handle semantics failed");
        return 564;
    }
    puts("dynamic main-program handle semantics verified");

    void *handle = dlopen("/lib/libakaos-demo.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        char *error = dlerror();
        printf("dltest: dlopen failed (errno=%d, detail=%s)\n", errno,
               error ? error : "none");
        return 1;
    }
    void *resident = dlopen("/lib/libakaos-demo.so",
                             RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (resident != handle) {
        if (resident) (void)dlclose(resident);
        (void)dlclose(handle);
        puts("dltest: RTLD_NOLOAD missed a resident object");
        return 562;
    }
    if (dlclose(resident) != 0) {
        (void)dlclose(handle);
        puts("dltest: RTLD_NOLOAD handle release failed");
        return 563;
    }
    puts("dynamic shared object RTLD_NOLOAD semantics verified");
    if (dlsym(RTLD_DEFAULT, "akaos_shared_answer") != NULL) {
        printf("dltest: RTLD_LOCAL symbol leaked into global scope\n");
        (void)dlclose(handle);
        return 1;
    }
    answer_function answer = (answer_function)dlsym(handle, "akaos_shared_answer");
    if (!answer) {
        char *error = dlerror();
        printf("dltest: %s\n", error ? error : "dlsym failed");
        (void)dlclose(handle);
        return 1;
    }
    int result = answer();
    Dl_info answer_info;
    if (!dladdr((const void *)answer, &answer_info) ||
        !answer_info.dli_fname ||
        strcmp(answer_info.dli_fname, "/lib/libakaos-demo.so") != 0 ||
        !answer_info.dli_sname ||
        strcmp(answer_info.dli_sname, "akaos_shared_answer") != 0) {
        puts("dltest: dynamic symbol address lookup failed");
        (void)dlclose(handle);
        return 1;
    }
    puts("dynamic symbol address lookup verified");
    struct phdr_probe_context phdr_context = {0, 0, 0, 0};
    if (dl_iterate_phdr(inspect_loaded_program_headers, &phdr_context) != 0 ||
        !phdr_context.found_demo_library ||
        !phdr_context.found_demo_load_segment || phdr_context.invalid_image) {
        printf("dltest: ELF program-header enumeration failed (images=%u demo=%d load=%d invalid=%d)\n",
               phdr_context.image_count, phdr_context.found_demo_library,
               phdr_context.found_demo_load_segment,
               phdr_context.invalid_image);
        (void)dlclose(handle);
        return 1;
    }
    puts("dynamic program headers verified");
    void *cxx_handle = dlopen("/lib/libakaos-cxx.so",
                              RTLD_NOW | RTLD_LOCAL);
    if (!cxx_handle) {
        char *error = dlerror();
        printf("dltest: C++ DSO open failed (%s)\n",
               error ? error : "unknown error");
        (void)dlclose(handle);
        return 1;
    }
    int (*cxx_constructor_ran)(void) = (int (*)(void))dlsym(
        cxx_handle, "akaos_cxx_dso_constructor_ran");
    if (!cxx_constructor_ran || !cxx_constructor_ran()) {
        printf("dltest: C++ DSO constructor did not run\n");
        (void)dlclose(cxx_handle);
        (void)dlclose(handle);
        return 1;
    }
    puts("C++ DSO constructor verified");
    if (dlclose(cxx_handle) != 0) {
        printf("dltest: C++ DSO close failed\n");
        (void)dlclose(handle);
        return 1;
    }
    puts("C++ DSO load/close lifecycle verified");
    puts("dltest: entering dynamic TLS worker checks");
    puts("dltest: resolving the main-thread TLS variable");
    int *tls_value = (int *)dlsym(handle, "akaos_tls_probe_value");
    if (!tls_value || *tls_value != 37) {
        printf("dltest: TLS template lookup failed\n");
        (void)dlclose(handle);
        return 1;
    }
    puts("dltest: main-thread TLS variable resolved");
    *tls_value = 73;
    tls_read_function read_value =
        (tls_read_function)dlsym(handle, "akaos_tls_probe_read");
    tls_write_function write_value =
        (tls_write_function)dlsym(handle, "akaos_tls_probe_write");
    if (!read_value || !write_value || read_value() != 73) {
        printf("dltest: TLS access sequence lookup failed\n");
        (void)dlclose(handle);
        return 1;
    }
    puts("dltest: main-thread TLS accessor passed");
    pthread_t tls_thread;
    puts("dltest: creating the TLS worker");
    if (pthread_create(&tls_thread, NULL, tls_worker, handle) != 0) {
        printf("dltest: TLS worker creation failed\n");
        (void)dlclose(handle);
        return 1;
    }
    puts("dltest: TLS worker created");
    void *tls_worker_result = NULL;
    if (pthread_join(tls_thread, &tls_worker_result) != 0 ||
        (uintptr_t)tls_worker_result != 84 || *tls_value != 73) {
        printf("dltest: TLS thread isolation failed\n");
        (void)dlclose(handle);
        return 1;
    }
    puts("dltest: TLS worker isolation passed");
    constructor_status_function constructor_status =
        (constructor_status_function)dlsym(handle, "akaos_shared_constructor_ran");
    if (!constructor_status || !constructor_status()) {
        char *error = dlerror();
        printf("dltest: shared object constructor failed (%s)\n",
               error ? error : "status was not initialized");
        (void)dlclose(handle);
        return 1;
    }
    puts("dltest: dynamic TLS worker checks passed");
    void *versioned_handle =
        dlopen("/lib/libakaos-versioned.so", RTLD_NOW | RTLD_GLOBAL);
    if (!versioned_handle) {
        char *error = dlerror();
        printf("dltest: versioned dlopen failed (errno=%d, detail=%s)\n",
               errno, error ? error : "none");
        (void)dlclose(handle);
        return 1;
    }
    answer_function versioned_default =
        (answer_function)dlsym(versioned_handle, "akaos_versioned_answer");
    answer_function versioned_v1 =
        (answer_function)dlvsym(versioned_handle, "akaos_versioned_answer",
                                "AKAOS_1.0");
    answer_function versioned_v2 =
        (answer_function)dlvsym(versioned_handle, "akaos_versioned_answer",
                                "AKAOS_2.0");
    if (!versioned_default || !versioned_v1 || !versioned_v2 ||
        versioned_default() != 22 || versioned_v1() != 11 ||
        versioned_v2() != 22) {
        printf("dltest: symbol-version lookup failed\n");
        (void)dlclose(versioned_handle);
        (void)dlclose(handle);
        return 1;
    }
    answer_function default_scope =
        (answer_function)dlsym(RTLD_DEFAULT, "akaos_versioned_answer");
    answer_function default_scope_v1 =
        (answer_function)dlvsym(RTLD_DEFAULT, "akaos_versioned_answer",
                                "AKAOS_1.0");
    if (!default_scope || !default_scope_v1 || default_scope() != 22 ||
        default_scope_v1() != 11) {
        printf("dltest: global dlsym scope failed\n");
        (void)dlclose(versioned_handle);
        (void)dlclose(handle);
        return 1;
    }
    if (dlclose(versioned_handle) != 0) {
        printf("dltest: versioned DSO close failed\n");
        (void)dlclose(handle);
        return 1;
    }
    puts("dltest: symbol-version checks passed");
    void *dependent_handle = dlopen("/lib/libakaos-dependent.so", RTLD_NOW | RTLD_LOCAL);
    if (!dependent_handle) {
        char *error = dlerror();
        printf("dltest: dependent dlopen failed (errno=%d, detail=%s)\n", errno,
               error ? error : "none");
        (void)dlclose(handle);
        return 1;
    }
    answer_function dependent_answer =
        (answer_function)dlsym(dependent_handle, "akaos_dependent_answer");
    answer_function versioned_dependent_answer =
        (answer_function)dlsym(dependent_handle,
                               "akaos_versioned_dependent_answer");
    /* The provider is a DT_NEEDED dependency of the dependent DSO.  An
     * explicit-handle lookup must see that transitive scope just as a normal
     * plugin loader does. */
    answer_function transitive_versioned_answer =
        (answer_function)dlsym(dependent_handle, "akaos_versioned_answer");
    if (!dependent_answer || !versioned_dependent_answer ||
        !transitive_versioned_answer) {
        char *error = dlerror();
        printf("dltest: dependent %s\n", error ? error : "dlsym failed");
        (void)dlclose(dependent_handle);
        (void)dlclose(handle);
        return 1;
    }
    int dependent_result = dependent_answer();
    int versioned_dependent_result = versioned_dependent_answer();
    int transitive_versioned_result = transitive_versioned_answer();
    if (dlclose(dependent_handle) != 0 || dlclose(handle) != 0 ||
        result != 42 || dependent_result != 42 ||
        versioned_dependent_result != 11 ||
        transitive_versioned_result != 22) {
        printf("dltest: shared call returned %d\n", result);
        return 1;
    }
    puts("dltest: dependency lookup checks passed");
    void *fanout_handle =
        dlopen("/lib/libakaos-loader-fanout.so", RTLD_NOW | RTLD_LOCAL);
    answer_function dependency_fanout = fanout_handle
        ? (answer_function)dlsym(fanout_handle,
                                 "akaos_loader_dependency_fanout")
        : NULL;
    if (!dependency_fanout || dependency_fanout() != 15 ||
        dlclose(fanout_handle) != 0) {
        char *error = dlerror();
        printf("dltest: five-provider dependency fanout failed (%s)\n",
               error ? error : "wrong result or close failure");
        if (fanout_handle) (void)dlclose(fanout_handle);
        return 1;
    }
    puts("dltest: dependency fanout checks passed");
    /* Both references to the TLS-bearing demo DSO are gone now.  Reload it
     * and require a fresh PT_TLS template so plugin unload/reload cannot
     * retain stale per-thread state. */
    void *reloaded_handle =
        dlopen("/lib/libakaos-demo.so", RTLD_NOW | RTLD_LOCAL);
    int *reloaded_tls = reloaded_handle
        ? (int *)dlsym(reloaded_handle, "akaos_tls_probe_value") : NULL;
    if (!reloaded_handle || !reloaded_tls || *reloaded_tls != 37 ||
        dlclose(reloaded_handle) != 0) {
        printf("dltest: TLS DSO reclamation failed\n");
        if (reloaded_handle) (void)dlclose(reloaded_handle);
        return 1;
    }
    puts("dltest: dynamic TLS reclamation checks passed");
    void *static_handle =
        dlopen("/lib/libakaos-static.so", RTLD_NOW | RTLD_LOCAL);
    if (!static_handle) {
        char *error = dlerror();
        printf("dltest: static TLS dlopen failed (errno=%d, detail=%s)\n",
               errno, error ? error : "none");
        return 1;
    }
    tls_read_function static_read =
        (tls_read_function)dlsym(static_handle, "akaos_static_tls_read");
    tls_write_function static_write =
        (tls_write_function)dlsym(static_handle, "akaos_static_tls_write");
    tls_address_function static_alignment = (tls_address_function)dlsym(
        static_handle, "akaos_static_tls_alignment_address");
    int static_initial_value = static_read ? static_read() : 0;
    void *static_initial_alignment =
        static_alignment ? static_alignment() : NULL;
    if (!static_read || !static_write || !static_alignment ||
        static_initial_value != 55 ||
        ((uintptr_t)static_initial_alignment & 63U) != 0U) {
        printf("dltest: static TLS initial value failed (value=%d address=%p alignment=%lu)\n",
               static_initial_value, static_initial_alignment,
               (unsigned long)((uintptr_t)static_initial_alignment & 63U));
        (void)dlclose(static_handle);
        return 1;
    }
    static_write(99);
    pthread_t static_thread;
    void *static_result = NULL;
    if (pthread_create(&static_thread, NULL, static_tls_worker,
                       static_handle) != 0 ||
        pthread_join(static_thread, &static_result) != 0 ||
        (uintptr_t)static_result != 123 || static_read() != 99) {
        printf("dltest: static TLS thread isolation failed\n");
        (void)dlclose(static_handle);
        return 1;
    }
    if (dlclose(static_handle) != 0) {
        printf("dltest: static TLS DSO close failed\n");
        return 1;
    }
    puts("dltest: static TLS worker checks passed");
    for (unsigned reload = 0; reload < 12U; ++reload) {
        static_handle = dlopen("/lib/libakaos-static.so", RTLD_NOW | RTLD_LOCAL);
        static_read = static_handle
            ? (tls_read_function)dlsym(static_handle, "akaos_static_tls_read")
            : NULL;
        static_alignment = static_handle
            ? (tls_address_function)dlsym(
                  static_handle, "akaos_static_tls_alignment_address")
            : NULL;
        if (!static_handle || !static_read || !static_alignment ||
            static_read() != 55 ||
            ((uintptr_t)static_alignment() & 63U) != 0U ||
            dlclose(static_handle) != 0) {
            printf("dltest: static TLS reload/reuse failed at iteration %u\n",
                   reload);
            if (static_handle) (void)dlclose(static_handle);
            return 1;
        }
    }
    puts("dltest: static TLS reload checks passed");
    /* Real desktop processes load far more DSOs than the handful used by the
     * functional probes above. Keep enough distinct paths live to cross the
     * old eight-object loader ceiling (the main executable occupies a slot),
     * then validate lookup and reference-counted teardown for every handle. */
    static const char *capacity_paths[] = {
        "/lib/libakaos-capacity-00.so", "/lib/libakaos-capacity-01.so",
        "/lib/libakaos-capacity-02.so", "/lib/libakaos-capacity-03.so",
        "/lib/libakaos-capacity-04.so", "/lib/libakaos-capacity-05.so",
        "/lib/libakaos-capacity-06.so", "/lib/libakaos-capacity-07.so",
        "/lib/libakaos-capacity-08.so", "/lib/libakaos-capacity-09.so",
        "/lib/libakaos-capacity-10.so", "/lib/libakaos-capacity-11.so",
    };
    void *capacity_handles[sizeof(capacity_paths) / sizeof(capacity_paths[0])] =
        {0};
    size_t capacity_count = sizeof(capacity_handles) / sizeof(capacity_handles[0]);
    size_t capacity_opened = 0;
    for (; capacity_opened < capacity_count; ++capacity_opened) {
        capacity_handles[capacity_opened] =
            dlopen(capacity_paths[capacity_opened], RTLD_NOW | RTLD_LOCAL);
        if (!capacity_handles[capacity_opened]) {
            char *error = dlerror();
            printf("dltest: loader capacity open %u failed (errno=%d, detail=%s)\n",
                   (unsigned)capacity_opened, errno,
                   error ? error : "none");
            while (capacity_opened != 0U)
                (void)dlclose(capacity_handles[--capacity_opened]);
            return 1;
        }
        answer_function capacity_answer = (answer_function)dlsym(
            capacity_handles[capacity_opened], "akaos_versioned_answer");
        if (!capacity_answer || capacity_answer() != 22) {
            printf("dltest: loader capacity symbol lookup failed at %u\n",
                   (unsigned)capacity_opened);
            ++capacity_opened;
            while (capacity_opened != 0U)
                (void)dlclose(capacity_handles[--capacity_opened]);
            return 1;
        }
    }
    while (capacity_opened != 0U) {
        if (dlclose(capacity_handles[--capacity_opened]) != 0) {
            printf("dltest: loader capacity close failed\n");
            return 1;
        }
    }
    puts("dltest: loader capacity checks passed");
    if (!run_dlerror_isolation_test()) {
        printf("dltest: per-thread dlerror isolation failed\n");
        return 1;
    }
    puts("dltest: per-thread dlerror checks passed");
    if (!run_concurrent_loader_test()) {
        printf("dltest: concurrent DSO lifecycle failed\n");
        return 1;
    }
    puts("dynamic shared object symbol versions verified");
    puts("dynamic shared object verified (answer=42)");
    puts("dynamic shared object dependency verified (answer=42)");
    puts("dynamic shared object constructor verified");
    puts("dynamic shared object TLS verified");
    puts("dynamic shared object TLS reclamation verified");
    puts("dynamic shared object static TLS verified");
    puts("dynamic shared object dependency fanout verified (5 providers)");
    puts("dynamic shared object capacity verified (12 live handles)");
    puts("dynamic shared object per-thread dlerror verified");
    puts("dynamic shared object concurrent lifecycle verified (4 workers)");
    return 0;
}
