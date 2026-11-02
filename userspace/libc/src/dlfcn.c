#include <dlfcn.h>

#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN 3
#define PT_DYNAMIC 2
#define PT_TLS 7
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_HASH 4
#define DT_GNU_HASH 0x6ffffef5
#define DT_INIT 12
#define DT_FINI 13
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_VERSYM 0x6ffffff0
#define DT_VERDEF 0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd
#define DT_VERNEED 0x6ffffffe
#define DT_VERNEEDNUM 0x6fffffff
#define SHN_UNDEF 0
#define STB_WEAK 2
#define STT_TLS 6
#define AKAOS_VERSYM_HIDDEN UINT16_C(0x8000)
#define AKAOS_VERSYM_INDEX_MASK UINT16_C(0x7fff)

/* Keep constructor tables deliberately bounded.  A desktop DSO normally has
 * only a handful of entries; the bound prevents a malformed image from
 * turning dlopen into an unbounded callback walk. */
#define AKAOS_INIT_ARRAY_MAX 4096U
#define AKAOS_TLS_THREAD_SLOTS 64U
#define AKAOS_TLS_IMAGE_MAX (64U * 1024U)
#define AKAOS_VERSION_MAX 4096U
#define AKAOS_STATIC_TLS_THREAD_SLOTS 64U
#define AKAOS_STATIC_TLS_OBJECT_SLOTS 256U
#define AKAOS_DSO_IMAGE_MAX (32U * 1024U * 1024U)
#define AKAOS_DSO_PATH_MAX 256U
#define AKAOS_DL_DEPENDENCY_MAX 128U
#if defined(__i386__)
/* The i386 kernel bounds per-process mmap metadata at 128 pages. Keep each
 * thread's static-TLS reservation small enough that dynamically loaded
 * desktop runtimes retain address space for their DSOs; current initial-exec
 * coverage needs an 8 KiB TLS module plus the main image. */
#define AKAOS_STATIC_TLS_AREA_SIZE (16U * 1024U)
#else
#define AKAOS_STATIC_TLS_AREA_SIZE (64U * 1024U)
#endif
#if defined(__riscv)
/* Keep tp 16-byte aligned on both RV32 and RV64.  RV32 has 32-bit words, but
 * its initial TLS image is still aligned to 16 bytes and begins at tp + 0. */
#define AKAOS_STATIC_TLS_TCB_SIZE 16U
#else
#define AKAOS_STATIC_TLS_TCB_SIZE (2U * sizeof(uintptr_t))
#endif

#if UINTPTR_MAX > UINT32_MAX
#define AKAOS_ELF64 1
typedef uint64_t elf_addr_t;
typedef uint64_t elf_off_t;
typedef uint64_t elf_word_t;
typedef int64_t elf_sword_t;
typedef uint16_t elf_half_t;
#define ELF_CLASS ELFCLASS64
#else
#define AKAOS_ELF64 0
typedef uint32_t elf_addr_t;
typedef uint32_t elf_off_t;
typedef uint32_t elf_word_t;
typedef int32_t elf_sword_t;
typedef uint16_t elf_half_t;
#define ELF_CLASS ELFCLASS32
#endif

struct elf_ehdr {
    unsigned char ident[16];
    elf_half_t type;
    elf_half_t machine;
    uint32_t version;
    elf_addr_t entry;
    elf_off_t phoff;
    elf_off_t shoff;
    uint32_t flags;
    elf_half_t ehsize;
    elf_half_t phentsize;
    elf_half_t phnum;
    elf_half_t shentsize;
    elf_half_t shnum;
    elf_half_t shstrndx;
};

#if AKAOS_ELF64
struct elf_phdr {
    uint32_t type;
    uint32_t flags;
    elf_off_t offset;
    elf_addr_t vaddr;
    elf_addr_t paddr;
    elf_word_t filesz;
    elf_word_t memsz;
    elf_word_t align;
};
struct elf_dyn {
    elf_sword_t tag;
    elf_word_t value;
};
struct elf_sym {
    uint32_t name;
    unsigned char info;
    unsigned char other;
    elf_half_t shndx;
    elf_addr_t value;
    elf_word_t size;
};
struct elf_rela {
    elf_addr_t offset;
    uint64_t info;
    elf_sword_t addend;
};
struct elf_rel {
    elf_addr_t offset;
    uint64_t info;
};
#else
struct elf_phdr {
    elf_word_t type;
    elf_off_t offset;
    elf_addr_t vaddr;
    elf_addr_t paddr;
    elf_word_t filesz;
    elf_word_t memsz;
    elf_word_t flags;
    elf_word_t align;
};
struct elf_dyn {
    elf_sword_t tag;
    elf_word_t value;
};
struct elf_sym {
    elf_word_t name;
    elf_addr_t value;
    elf_word_t size;
    unsigned char info;
    unsigned char other;
    elf_half_t shndx;
};
struct elf_rel {
    elf_addr_t offset;
    elf_word_t info_lo;
};
struct elf_rela {
    elf_addr_t offset;
    elf_word_t info_lo;
    elf_sword_t addend;
};
#endif

/* The compiler's general-dynamic TLS sequence passes a two-word descriptor
 * to this libc entry point.  The concrete definition lives in pthread.c;
 * this forward declaration lets the loader resolve an undefined PLT entry
 * without requiring the main executable to publish its entire symbol table. */
struct akaos_tls_index;
#if defined(__i386__)
extern void *__tls_get_addr(const struct akaos_tls_index *index)
    __attribute__((regparm(1)));
#else
extern void *__tls_get_addr(const struct akaos_tls_index *index);
#endif

#if defined(__aarch64__)
/* AArch64's TLSDESC sequence calls this resolver with the address of the
 * two-word descriptor in x0.  The definition is below the object table, but
 * the relocation path needs its address while applying relocations. */
extern uintptr_t akaos_aarch64_tlsdesc_resolver(const uintptr_t *descriptor);
#endif
#if defined(__arm__)
extern void *__aeabi_read_tp(void);
#endif

#if AKAOS_ELF64
_Static_assert(sizeof(struct elf_ehdr) == 64, "ELF64 header layout");
_Static_assert(sizeof(struct elf_phdr) == 56, "ELF64 program layout");
_Static_assert(sizeof(struct elf_sym) == 24, "ELF64 symbol layout");
_Static_assert(sizeof(struct elf_rel) == 16, "ELF64 REL layout");
_Static_assert(sizeof(struct elf_rela) == 24, "ELF64 RELA layout");
#else
_Static_assert(sizeof(struct elf_ehdr) == 52, "ELF32 header layout");
_Static_assert(sizeof(struct elf_phdr) == 32, "ELF32 program layout");
_Static_assert(sizeof(struct elf_sym) == 16, "ELF32 symbol layout");
_Static_assert(sizeof(struct elf_rel) == 8, "ELF32 REL layout");
_Static_assert(sizeof(struct elf_rela) == 12, "ELF32 RELA layout");
#endif

/* ELF symbol-version records use the same fixed-width layout in ELF32 and
 * ELF64.  All offsets are relative to their containing record and are
 * validated against the mapped object before they are dereferenced. */
struct elf_verdef {
    uint16_t version;
    uint16_t flags;
    uint16_t index;
    uint16_t count;
    uint32_t hash;
    uint32_t aux;
    uint32_t next;
};
struct elf_verdaux {
    uint32_t name;
    uint32_t next;
};
struct elf_verneed {
    uint16_t version;
    uint16_t count;
    uint32_t file;
    uint32_t aux;
    uint32_t next;
};
struct elf_vernaux {
    uint32_t hash;
    uint16_t flags;
    uint16_t other;
    uint32_t name;
    uint32_t next;
};

struct akaos_tlsdesc_entry {
    uintptr_t descriptor;
    uintptr_t module;
    uintptr_t offset;
};

struct dl_object {
    int used;
    int loading;
    int global_scope;
    uint32_t references;
    void *mapping;
    size_t mapping_length;
    struct elf_phdr *program_headers;
    elf_half_t program_header_count;
    uint64_t min_vaddr;
    uint64_t max_vaddr;
    uintptr_t load_bias;
    uintptr_t entry;
    uintptr_t init;
    uintptr_t fini;
    uintptr_t init_array;
    size_t init_array_count;
    uintptr_t fini_array;
    size_t fini_array_count;
    int initializers_ran;
    int finalizers_ran;
    uintptr_t tls_module;
    unsigned char *tls_template;
    size_t tls_filesz;
    size_t tls_size;
    size_t tls_align;
    size_t tls_mapping_length;
    /* ABI-relative offset of this module's PT_TLS image from the hardware
     * thread pointer.  Negative-TPOFF targets (x86/ARM) point down from a
     * TCB at the end of the static area; AArch64/RISC-V point up from TP. */
    intptr_t static_tls_offset;
    size_t static_tls_start;
    int static_tls_assigned;
    uint64_t static_tls_generation;
    uint32_t tls_thread_ids[AKAOS_TLS_THREAD_SLOTS];
    void *tls_blocks[AKAOS_TLS_THREAD_SLOTS];
    size_t tlsdesc_count;
    size_t tlsdesc_capacity;
    struct akaos_tlsdesc_entry *tlsdescs;
    const char *path;
    char path_storage[AKAOS_DSO_PATH_MAX];
    const char *strtab;
    size_t strtab_size;
    const struct elf_sym *symtab;
    size_t symbol_count;
    uint64_t gnu_hash_vaddr;
    const uint16_t *versym;
    uint64_t verdef_vaddr;
    uint32_t verdef_count;
    uint64_t verneed_vaddr;
    uint32_t verneed_count;
    uint32_t dependency_count;
    struct dl_object *dependencies[AKAOS_DL_DEPENDENCY_MAX];
};

#define DL_OBJECT_MAX 256
#define DL_ERROR_MAX 128
/* The DSO namespace can be large, but most static akaOS utilities never call
 * dlopen. Keep the namespace off their ELF .bss so the loader's capacity does
 * not consume executable image pages in every unrelated process. */
static struct dl_object *objects;
static uint64_t loader_object_add_count;
static uint64_t loader_object_sub_count;
static struct {
    struct elf_phdr *program_headers;
    elf_half_t program_header_count;
    uintptr_t load_bias;
    char path[AKAOS_DSO_PATH_MAX];
} main_image;
/* dlopen(NULL, ...) returns a stable process-wide handle that is distinct
 * from RTLD_DEFAULT (which is intentionally represented by a null pointer). */
static unsigned char main_module_handle;
static pthread_mutex_t loader_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t loader_mutex_once = PTHREAD_ONCE_INIT;
static int loader_mutex_ready;
static int loader_atfork_ready;
static pthread_t loader_fork_thread;
static volatile uint32_t dlerror_table_lock;

struct dl_error_state {
    pthread_t thread;
    char last[DL_ERROR_MAX];
    char returned[DL_ERROR_MAX];
};

#define DL_ERROR_THREAD_SLOTS 64U
static struct dl_error_state dlerror_states[DL_ERROR_THREAD_SLOTS];

static void loader_fork_prepare(void);
static void loader_fork_parent(void);
static void loader_fork_child(void);

static void dlerror_table_acquire(void) {
    uint32_t expected;
    do {
        expected = 0U;
    } while (!__atomic_compare_exchange_n(&dlerror_table_lock, &expected, 1U,
                                          0, __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
}

static void dlerror_table_release(void) {
    __atomic_store_n(&dlerror_table_lock, 0U, __ATOMIC_RELEASE);
}

static struct dl_error_state *dlerror_state_locked(pthread_t thread,
                                                    int create) {
    if (!thread) return NULL;
    struct dl_error_state *free_state = NULL;
    for (size_t index = 0; index < DL_ERROR_THREAD_SLOTS; ++index) {
        struct dl_error_state *state = &dlerror_states[index];
        if (state->thread == thread) return state;
        if (state->thread == 0U && !free_state) free_state = state;
    }
    if (!create || !free_state) return NULL;
    memset(free_state, 0, sizeof(*free_state));
    free_state->thread = thread;
    return free_state;
}

static void initialize_loader_mutex(void) {
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes) != 0) return;
    if (pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE) == 0 &&
        pthread_mutex_init(&loader_mutex, &attributes) == 0)
        loader_mutex_ready = 1;
    (void)pthread_mutexattr_destroy(&attributes);
    if (loader_mutex_ready &&
        pthread_atfork(loader_fork_prepare, loader_fork_parent,
                       loader_fork_child) == 0)
        loader_atfork_ready = 1;
    else
        loader_mutex_ready = 0;
}

static int loader_lock(void) {
    if (pthread_once(&loader_mutex_once, initialize_loader_mutex) != 0 ||
        !loader_mutex_ready) return 0;
    return pthread_mutex_lock(&loader_mutex) == 0;
}

static void loader_unlock(void) {
    (void)pthread_mutex_unlock(&loader_mutex);
}

static void loader_fork_prepare(void) {
    if (!loader_lock()) return;
    loader_fork_thread = pthread_self();
    dlerror_table_acquire();
}

static void loader_fork_parent(void) {
    if (!loader_atfork_ready) return;
    loader_fork_thread = 0U;
    dlerror_table_release();
    loader_unlock();
}

static void loader_fork_child(void) {
    if (!loader_atfork_ready) return;
    pthread_t child = pthread_self();
    for (size_t index = 0; index < DL_ERROR_THREAD_SLOTS; ++index) {
        struct dl_error_state *state = &dlerror_states[index];
        if (state->thread == loader_fork_thread) {
            state->thread = child;
        } else if (state->thread != 0U) {
            memset(state, 0, sizeof(*state));
        }
    }
    loader_fork_thread = 0U;
    dlerror_table_release();
    if (child) loader_mutex.owner = child;
    /* The prepare callback added exactly one recursive level. Releasing that
     * level leaves any lock levels owned by a constructor that called fork()
     * intact, while updating their owner to the sole surviving child thread. */
    (void)pthread_mutex_unlock(&loader_mutex);
}

struct akaos_static_tls_thread {
    pthread_t thread;
    void *mapping;
    uintptr_t thread_pointer;
    uint64_t generation;
    uint64_t object_generations[AKAOS_STATIC_TLS_OBJECT_SLOTS];
};

static struct akaos_static_tls_thread static_tls_thread_storage[
    AKAOS_STATIC_TLS_THREAD_SLOTS];
/* Keep this pointer zero-initialized so static ET_EXEC images avoid an
 * unsupported absolute data relocation to the local TLS table. A dynamic libc
 * is rebound to the interpreter's table before application code runs. */
static struct akaos_static_tls_thread *static_tls_threads;
static uint64_t static_tls_generation = 1;
static uint32_t static_tls_object_count;

static void ensure_static_tls_thread_table(void) {
    if (!static_tls_threads) static_tls_threads = static_tls_thread_storage;
}

#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
#define AKAOS_STATIC_TLS_POSITIVE 1
#else
#define AKAOS_STATIC_TLS_POSITIVE 0
#endif

#if defined(__aarch64__) || defined(__arm__)
/* Arm Variant I places the TCB at TP and static TLS blocks after it. */
#define AKAOS_STATIC_TLS_TP_OFFSET 0U
#define AKAOS_STATIC_TLS_DATA_START AKAOS_STATIC_TLS_TCB_SIZE
#define AKAOS_STATIC_TLS_DATA_LIMIT AKAOS_STATIC_TLS_AREA_SIZE
#elif defined(__riscv)
/* RISC-V Variant I has the TCB immediately before TP and TLS at TP + 0. */
#define AKAOS_STATIC_TLS_TP_OFFSET AKAOS_STATIC_TLS_TCB_SIZE
#define AKAOS_STATIC_TLS_DATA_START 0U
#define AKAOS_STATIC_TLS_DATA_LIMIT \
    (AKAOS_STATIC_TLS_AREA_SIZE - AKAOS_STATIC_TLS_TCB_SIZE)
#else
/* Negative-offset TLS variants address the block below TP. */
#define AKAOS_STATIC_TLS_DATA_START 0U
#define AKAOS_STATIC_TLS_DATA_LIMIT AKAOS_STATIC_TLS_AREA_SIZE
#endif

#if !AKAOS_STATIC_TLS_POSITIVE
static size_t static_tls_tp_offset_value =
    AKAOS_STATIC_TLS_AREA_SIZE - AKAOS_STATIC_TLS_TCB_SIZE;

static size_t static_tls_tp_offset(void) {
    return static_tls_tp_offset_value;
}
#endif

static size_t static_tls_align_up(size_t value, size_t alignment);
static size_t static_tls_align_thread_offset(size_t value,
                                             size_t alignment);

static int static_tls_find_range(size_t size, size_t alignment,
                                 size_t *start_out) {
    if (!objects || !start_out || size == 0) return 0;
#if AKAOS_STATIC_TLS_POSITIVE
    const size_t limit = AKAOS_STATIC_TLS_DATA_LIMIT;
#else
    const size_t limit = static_tls_tp_offset();
#endif
    size_t candidate = static_tls_align_thread_offset(
        AKAOS_STATIC_TLS_DATA_START, alignment);
    for (;;) {
        if (candidate == SIZE_MAX || candidate > limit ||
            size > limit - candidate) return 0;
        int overlaps = 0;
        for (int index = 0; index < DL_OBJECT_MAX; ++index) {
            const struct dl_object *other = &objects[index];
            if (!other->used || !other->static_tls_assigned ||
                other->tls_size == 0) continue;
            size_t other_start = other->static_tls_start;
            if (candidate < other_start + other->tls_size &&
                other_start < candidate + size) {
                size_t next = other_start + other->tls_size;
                if (next <= candidate) return 0;
                candidate = static_tls_align_thread_offset(next, alignment);
                overlaps = 1;
                break;
            }
        }
        if (overlaps) continue;
        *start_out = candidate;
        return 1;
    }
}

static size_t static_tls_align_up(size_t value, size_t alignment) {
    if (alignment <= 1) return value;
    size_t mask = alignment - 1;
    if ((alignment & mask) != 0 || value > SIZE_MAX - mask) return SIZE_MAX;
    return (value + mask) & ~mask;
}

static size_t static_tls_align_thread_offset(size_t value,
                                             size_t alignment) {
#if AKAOS_STATIC_TLS_POSITIVE
    if ((size_t)AKAOS_STATIC_TLS_TP_OFFSET > SIZE_MAX - value)
        return SIZE_MAX;
    size_t aligned_address = static_tls_align_up(
        (size_t)AKAOS_STATIC_TLS_TP_OFFSET + value, alignment);
    if (aligned_address == SIZE_MAX ||
        aligned_address < (size_t)AKAOS_STATIC_TLS_TP_OFFSET)
        return SIZE_MAX;
    return aligned_address - (size_t)AKAOS_STATIC_TLS_TP_OFFSET;
#else
    return static_tls_align_up(value, alignment);
#endif
}

static void static_tls_copy_objects(struct akaos_static_tls_thread *thread,
                                    int reset_all);

static void reset_object(struct dl_object *object) {
    if (!object) return;
    for (size_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
        if (object->tls_blocks[slot]) {
            (void)munmap(object->tls_blocks[slot], object->tls_mapping_length);
        }
    }
    if (object->static_tls_assigned && static_tls_object_count != 0U)
        --static_tls_object_count;
    free(object->tls_template);
    free(object->tlsdescs);
    free(object->program_headers);
    memset(object, 0, sizeof(*object));
}

static void close_dependencies(struct dl_object *object) {
    if (!object) return;
    while (object->dependency_count) {
        struct dl_object *dependency =
            object->dependencies[--object->dependency_count];
        object->dependencies[object->dependency_count] = NULL;
        if (dependency) (void)dlclose(dependency);
    }
}

/* DSO files routinely exceed XSCore's deliberately small initial brk heap.
 * Keep the temporary ELF-file copy page-backed so a normal C++ runtime can be
 * loaded without raising every process's allocator/heap reservation. */
static void release_loader_image(unsigned char *image, size_t length) {
    if (image && image != MAP_FAILED) (void)munmap(image, length);
}

static void set_error(const char *message) {
    dlerror_table_acquire();
    struct dl_error_state *state =
        dlerror_state_locked(pthread_self(), 1);
    if (!state) {
        dlerror_table_release();
        return;
    }
    size_t length = strlen(message);
    if (length >= DL_ERROR_MAX) length = DL_ERROR_MAX - 1U;
    memcpy(state->last, message, length);
    state->last[length] = '\0';
    dlerror_table_release();
}

static void clear_error(void) {
    dlerror_table_acquire();
    struct dl_error_state *state =
        dlerror_state_locked(pthread_self(), 0);
    if (state) state->last[0] = '\0';
    dlerror_table_release();
}

static int ensure_object_table(void) {
    ensure_static_tls_thread_table();
    if (objects) return 1;
    struct dl_object *table =
        (struct dl_object *)calloc(DL_OBJECT_MAX, sizeof(*table));
    if (!table) {
        set_error("dlopen: object table allocation failed");
        errno = ENOMEM;
        return 0;
    }
    objects = table;
    return 1;
}

static int static_tls_assign_object(struct dl_object *object) {
    if (!object || object->tls_size == 0) return 1;
    size_t alignment = object->tls_align;
    if (alignment == 0) alignment = sizeof(uintptr_t);
    if (alignment > AKAOS_STATIC_TLS_AREA_SIZE ||
        (alignment & (alignment - 1)) != 0) {
        set_error("dlopen: invalid static TLS alignment");
        errno = EINVAL;
        return 0;
    }
    size_t start = 0;
    if (!static_tls_find_range(object->tls_size, alignment, &start)) {
        set_error("dlopen: static TLS area exhausted");
        errno = ENOMEM;
        return 0;
    }
    size_t end = start + object->tls_size;
#if AKAOS_STATIC_TLS_POSITIVE
    if (end > AKAOS_STATIC_TLS_DATA_LIMIT) {
        set_error("dlopen: static TLS area exhausted");
        errno = ENOMEM;
        return 0;
    }
    object->static_tls_offset = (intptr_t)start;
#else
    size_t tp_offset = static_tls_tp_offset();
    if (end > tp_offset || start > tp_offset) {
        set_error("dlopen: static TLS area exhausted");
        errno = ENOMEM;
        return 0;
    }
    object->static_tls_offset = (intptr_t)start - (intptr_t)tp_offset;
#endif
    object->static_tls_start = start;
    ++static_tls_generation;
    if (static_tls_generation == 0) static_tls_generation = 1;
    object->static_tls_assigned = 1;
    object->static_tls_generation = static_tls_generation;
    ++static_tls_object_count;
    for (size_t index = 0; index < AKAOS_STATIC_TLS_THREAD_SLOTS; ++index) {
        if (!static_tls_threads[index].thread) continue;
        static_tls_copy_objects(&static_tls_threads[index], 0);
        static_tls_threads[index].generation = static_tls_generation;
    }
    return 1;
}

static unsigned char *static_tls_object_address(
    const struct akaos_static_tls_thread *thread,
    const struct dl_object *object, uintptr_t offset) {
    if (!thread || !thread->mapping || !object ||
        !object->static_tls_assigned || offset >= object->tls_size) return NULL;
    intptr_t relative = object->static_tls_offset + (intptr_t)offset;
    if (relative < 0) {
        uintptr_t magnitude = (uintptr_t)(-(relative + 1)) + 1U;
        if (magnitude > thread->thread_pointer) return NULL;
        return (unsigned char *)thread->thread_pointer - magnitude;
    }
    if ((uintptr_t)relative >= AKAOS_STATIC_TLS_DATA_LIMIT) return NULL;
    return (unsigned char *)thread->thread_pointer + (uintptr_t)relative;
}

static void static_tls_copy_objects(struct akaos_static_tls_thread *thread,
                                    int reset_all) {
    ensure_static_tls_thread_table();
    if (!thread || !thread->mapping) return;
#if AKAOS_STATIC_TLS_POSITIVE
    uintptr_t *tcb;
#if defined(__aarch64__) || defined(__arm__)
    tcb = (uintptr_t *)thread->thread_pointer;
#else
    tcb = (uintptr_t *)(thread->thread_pointer -
                        AKAOS_STATIC_TLS_TCB_SIZE);
#endif
    tcb[0] = thread->thread_pointer;
    tcb[1] = thread->thread_pointer;
#else
    /* The first TCB word is the value returned by the x86 GS/FS ABI. */
    *(uintptr_t *)thread->thread_pointer = thread->thread_pointer;
    ((uintptr_t *)thread->thread_pointer)[1] = thread->thread_pointer;
#endif
    if (!objects) return;
    for (int index = 0; index < DL_OBJECT_MAX; ++index) {
        struct dl_object *object = &objects[index];
        if (!object->used || !object->static_tls_assigned ||
            object->tls_size == 0) continue;
        if (!reset_all &&
            thread->object_generations[index] == object->static_tls_generation)
            continue;
        unsigned char *address = static_tls_object_address(thread, object, 0);
        if (!address) continue;
        memset(address, 0, object->tls_size);
        if (object->tls_template && object->tls_filesz != 0)
            memcpy(address, object->tls_template, object->tls_filesz);
        thread->object_generations[index] = object->static_tls_generation;
    }
}

static int static_tls_find_thread(pthread_t thread) {
    ensure_static_tls_thread_table();
    if (!thread) return -1;
    for (size_t index = 0; index < AKAOS_STATIC_TLS_THREAD_SLOTS; ++index)
        if (static_tls_threads[index].thread == thread) return (int)index;
    return -1;
}

static uintptr_t static_tls_prepare_locked(void) {
    if (static_tls_object_count == 0) return 0;
    pthread_t thread = pthread_self();
    if (!thread) {
        set_error("static TLS: no current thread");
        errno = ESRCH;
        return 0;
    }
    int index = static_tls_find_thread(thread);
    if (index < 0) {
        for (size_t candidate = 0;
             candidate < AKAOS_STATIC_TLS_THREAD_SLOTS; ++candidate) {
            if (static_tls_threads[candidate].thread != 0) continue;
            void *mapping = mmap(NULL, AKAOS_STATIC_TLS_AREA_SIZE,
                                 PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapping == MAP_FAILED) {
                set_error("static TLS: area allocation failed");
                errno = ENOMEM;
                return 0;
            }
            memset(mapping, 0, AKAOS_STATIC_TLS_AREA_SIZE);
            static_tls_threads[candidate].thread = thread;
            static_tls_threads[candidate].mapping = mapping;
#if AKAOS_STATIC_TLS_POSITIVE
            static_tls_threads[candidate].thread_pointer =
                (uintptr_t)mapping + AKAOS_STATIC_TLS_TP_OFFSET;
#else
            static_tls_threads[candidate].thread_pointer =
                (uintptr_t)mapping + static_tls_tp_offset();
#endif
            static_tls_threads[candidate].generation = 0;
            index = (int)candidate;
            break;
        }
    }
    if (index < 0) {
        set_error("static TLS: thread table exhausted");
        errno = ENOMEM;
        return 0;
    }
    struct akaos_static_tls_thread *slot = &static_tls_threads[index];
    if (slot->generation != static_tls_generation) {
        static_tls_copy_objects(slot, 0);
        slot->generation = static_tls_generation;
    }
    int tp_error = akaos_thread_pointer_set(slot->thread_pointer);
    if (tp_error != 0) {
        if (tp_error == EINVAL) set_error("static TLS: thread-pointer install returned EINVAL");
        else if (tp_error == ENOSYS) set_error("static TLS: thread-pointer syscall unavailable");
        else set_error("static TLS: thread-pointer install failed");
        return 0;
    }
    return slot->thread_pointer;
}

uintptr_t akaos_dl_static_tls_prepare(void) {
    if (!loader_lock()) {
        set_error("static TLS: loader mutex unavailable");
        errno = EAGAIN;
        return 0;
    }
    uintptr_t thread_pointer = static_tls_prepare_locked();
    loader_unlock();
    return thread_pointer;
}

static void static_tls_release_locked(void) {
    pthread_t thread = pthread_self();
    int index = static_tls_find_thread(thread);
    if (index < 0) return;
    struct akaos_static_tls_thread *slot = &static_tls_threads[index];
    void *mapping = slot->mapping;
    slot->thread = 0;
    slot->mapping = NULL;
    slot->thread_pointer = 0;
    slot->generation = 0;
    if (mapping) (void)munmap(mapping, AKAOS_STATIC_TLS_AREA_SIZE);
    (void)akaos_thread_pointer_set(0);
}

void akaos_dl_static_tls_release(void) {
    if (!loader_lock()) return;
    static_tls_release_locked();
    loader_unlock();
}

static unsigned char *akaos_static_tls_address(const struct dl_object *object,
                                                uintptr_t offset) {
    if (!object || !object->static_tls_assigned) return NULL;
    int index = static_tls_find_thread(pthread_self());
    if (index < 0) {
        if (!akaos_dl_static_tls_prepare()) return NULL;
        index = static_tls_find_thread(pthread_self());
    }
    if (index < 0) return NULL;
    return static_tls_object_address(&static_tls_threads[index], object, offset);
}

static int upstream_qtbase_enabled(void) {
    int saved_errno = errno;
    struct stat marker;
    int enabled = stat("/usr/local/lib/.upstream-qtbase", &marker) == 0;
    errno = saved_errno;
    return enabled;
}

static int upstream_library_exists(const char *name, size_t name_length) {
    static const char prefix[] = "/usr/local/lib/";
    char path[AKAOS_DSO_PATH_MAX];
    if (sizeof(prefix) - 1U + name_length >= sizeof(path)) return 0;
    memcpy(path, prefix, sizeof(prefix) - 1U);
    memcpy(path + sizeof(prefix) - 1U, name, name_length);
    path[sizeof(prefix) - 1U + name_length] = '\0';
    int saved_errno = errno;
    struct stat status;
    int exists = stat(path, &status) == 0;
    errno = saved_errno;
    return exists;
}

static const char *resolve_dlopen_library_path(const char *path,
                                               char *resolved_path,
                                               size_t resolved_capacity) {
    if (!path || strchr(path, '/') != NULL) return path;
    static const char *const prefixes[] = {
        "/usr/local/lib/", "/usr/lib/", "/lib/"
    };
    int saved_errno = errno;
    struct stat status;
    if (stat(path, &status) == 0) {
        errno = saved_errno;
        return path;
    }
    size_t name_length = strlen(path);
    for (size_t index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]);
         ++index) {
        size_t prefix_length = strlen(prefixes[index]);
        if (prefix_length + name_length >= resolved_capacity) continue;
        memcpy(resolved_path, prefixes[index], prefix_length);
        memcpy(resolved_path + prefix_length, path, name_length + 1U);
        if (stat(resolved_path, &status) == 0) {
            errno = saved_errno;
            return resolved_path;
        }
    }
    errno = saved_errno;
    return path;
}

/* Whether the loader writes a trace of what it maps and resolves.
 *
 * The trace is off unless the environment asks for it, because every program
 * with a shared library would pay for it. */
static int loader_trace_enabled(void) {
    static int state = -1;
    if (state < 0) {
        const char *value = getenv("AKAOS_LOADER_TRACE");
        if (value && value[0] && value[0] != '0') {
            state = 1;
        } else {
            /* The environment of the loader itself is not always set up, so the
             * name of the program is the second way to ask for the trace. */
            const char *path = main_image.path;
            state = (path && strstr(path, "openbox")) ? 1 : 0;
        }
    }
    return state;
}

static void loader_trace_path(const char *phase, const char *path) {
    static int trace_state = -1;
    static int trace_qt_active = 0;
    if (trace_state < 0) {
        int saved_errno = errno;
        struct stat marker;
        trace_state = stat("/etc/akaos-qt-loader-trace", &marker) == 0;
        errno = saved_errno;
    }
    if (!trace_state) return;
    if (strstr(path, "qtwayland-smoke") ||
        strstr(path, "lxqt-session") ||
        strstr(path, "/usr/local/lib/libQt6"))
        trace_qt_active = 1;
    if (!trace_qt_active) return;
    static const char prefix[] = "[ld] ";
    static const char separator[] = ": ";
    static const char newline[] = "\n";
    (void)write(STDERR_FILENO, prefix, sizeof(prefix) - 1U);
    (void)write(STDERR_FILENO, phase, strlen(phase));
    (void)write(STDERR_FILENO, separator, sizeof(separator) - 1U);
    (void)write(STDERR_FILENO, path, strlen(path));
    (void)write(STDERR_FILENO, newline, sizeof(newline) - 1U);
}

/* Packaged soname aliases are alternate spellings of one native runtime
 * object.  Canonicalizing them before the object-table lookup is important on
 * 32-bit guests: loading the same DSO once per alias needlessly consumes the
 * small per-process mmap budget and makes otherwise valid GTK/Qt startup fail
 * with address-space exhaustion. */
static const char *canonical_library_path(const char *path) {
    if (!path) return path;
    if (strcmp(path, "/lib/libintl.so") == 0) {
        int saved_errno = errno;
        struct stat marker;
        int has_upstream = stat("/lib/.upstream-glib", &marker) == 0;
        errno = saved_errno;
        if (has_upstream) return "/lib/libintl.so.8";
    }
    if (strcmp(path, "/lib/libc++.so") == 0)
        return "/lib/libc++.so.1";
    if (strcmp(path, "/lib/libc++abi.so") == 0)
        return "/lib/libc++abi.so.1";
    if (strcmp(path, "/lib/libunwind.so") == 0)
        return "/lib/libunwind.so.1";
    if (strcmp(path, "/lib/libpthread.so.0") == 0) return "/lib/libpthread.so";
    if (strcmp(path, "/lib/libglib-2.0.so.0") == 0 ||
        strcmp(path, "/lib/libgobject-2.0.so.0") == 0 ||
        strcmp(path, "/lib/libgio-2.0.so.0") == 0) {
        /* When the upstream runtime set is packaged, preserve the real
         * upstream soname paths.  The compatibility DSOs remain available
         * through libglib.so/libgio.so on all architectures. */
        int saved_errno = errno;
        struct stat marker;
        int has_upstream = stat("/lib/.upstream-glib", &marker) == 0;
        errno = saved_errno;
        if (has_upstream) return path;
        if (strcmp(path, "/lib/libgio-2.0.so.0") == 0)
            return "/lib/libgio.so";
        return "/lib/libglib.so";
    }
    if (strcmp(path, "/lib/libgtk-3.so.0") == 0 ||
        strcmp(path, "/lib/libgdk-3.so.0") == 0) {
        int saved_errno = errno;
        struct stat marker;
        int has_upstream = stat("/lib/.upstream-gtk3", &marker) == 0;
        errno = saved_errno;
        if (has_upstream) {
            if (strcmp(path, "/lib/libgdk-3.so.0") == 0)
                return "/lib/.upstream-gtk3/libgdk-3.so.0";
            return "/lib/.upstream-gtk3/libgtk-3.so.0";
        }
        return "/lib/libgtk.so";
    }
    if (strcmp(path, "/lib/libatk-1.0.so.0") == 0) {
        int saved_errno = errno;
        struct stat marker;
        int has_upstream = stat("/lib/.upstream-gtk3", &marker) == 0;
        errno = saved_errno;
        if (has_upstream) return "/lib/.upstream-gtk3/libatk-1.0.so.0";
    }
    if (strcmp(path, "/lib/libatk-bridge-2.0.so.0") == 0 ||
        strcmp(path, "/lib/libatspi.so.0") == 0) {
        /* The accessibility bridge belongs to the upstream GTK 3 runtime
         * set, so it is loaded from beside the other upstream libraries. */
        int saved_errno = errno;
        struct stat marker;
        int has_upstream = stat("/lib/.upstream-gtk3", &marker) == 0;
        errno = saved_errno;
        if (has_upstream) {
            if (strcmp(path, "/lib/libatspi.so.0") == 0)
                return "/lib/.upstream-gtk3/libatspi.so.0";
            return "/lib/.upstream-gtk3/libatk-bridge-2.0.so.0";
        }
    }
    if (strcmp(path, "/lib/libdbus-1.so.3") == 0) {
        /* The upstream message bus lives in the local prefix. */
        int saved_errno = errno;
        struct stat status;
        int found = stat("/usr/local/lib/libdbus-1.so.3", &status) == 0;
        errno = saved_errno;
        if (found) return "/usr/local/lib/libdbus-1.so.3";
    }
    if (strcmp(path, "/lib/libgtk-4.so.1") == 0 ||
        strcmp(path, "/lib/libgdk-4.so.1") == 0) {
        /* Keep the native compatibility alias available for existing akaOS
         * clients, while routing upstream GTK sonames to the real GNOME DSO
         * when that runtime set is installed in the image. */
        int saved_errno = errno;
        struct stat marker;
        int has_upstream = stat("/lib/.upstream-gtk", &marker) == 0;
        errno = saved_errno;
        if (has_upstream) return "/lib/.upstream-gtk/libgtk-4.so.1";
        return "/lib/libgtk-4.so";
    }
    if (strcmp(path, "/lib/libQt6Core.so.6") == 0)
        return "/lib/libQt6Core.so";
    if (strcmp(path, "/lib/libQt6Gui.so.6") == 0)
        return "/lib/libQt6Gui.so";
    if (strcmp(path, "/lib/libQt6Widgets.so.6") == 0)
        return "/lib/libQt6Widgets.so";
    return path;
}

char *dlerror(void) {
    dlerror_table_acquire();
    struct dl_error_state *state =
        dlerror_state_locked(pthread_self(), 0);
    if (!state || state->last[0] == '\0') {
        dlerror_table_release();
        return NULL;
    }
    memcpy(state->returned, state->last, sizeof(state->returned));
    state->last[0] = '\0';
    char *message = state->returned;
    dlerror_table_release();
    return message;
}

void akaos_dl_thread_cleanup(pthread_t thread) {
    dlerror_table_acquire();
    struct dl_error_state *state = dlerror_state_locked(thread, 0);
    if (state) memset(state, 0, sizeof(*state));
    dlerror_table_release();
}

static int range_ok(size_t file_size, uint64_t offset, uint64_t length) {
    return offset <= file_size && length <= (uint64_t)file_size - offset;
}

static uintptr_t mapped_address(const struct dl_object *object, uint64_t vaddr) {
    return object->load_bias + (uintptr_t)vaddr;
}

static int mapping_range_ok(const struct dl_object *object, uint64_t vaddr,
                            uint64_t length) {
    if (!object || vaddr < object->min_vaddr ||
        vaddr - object->min_vaddr > object->mapping_length) return 0;
    return length <= (uint64_t)object->mapping_length -
                    (vaddr - object->min_vaddr);
}

static int mapped_callback(const struct dl_object *object, uint64_t vaddr,
                           uintptr_t *callback) {
    if (!callback) return 0;
    if (vaddr == 0) {
        *callback = 0;
        return 1;
    }
    if (!mapping_range_ok(object, vaddr, 1)) return 0;
    *callback = mapped_address(object, vaddr);
    return 1;
}

static int callback_in_object(const struct dl_object *object,
                              uintptr_t callback) {
    if (!object || !callback || callback == UINTPTR_MAX || !object->mapping)
        return 0;
    uintptr_t begin = (uintptr_t)object->mapping;
    if (callback < begin || callback - begin >= object->mapping_length)
        return 0;
    return 1;
}

static void invoke_callback(uintptr_t callback) {
    if (callback) ((void (*)(void))callback)();
}

/* Resolve one module's TLS block for the calling akaOS thread.  This keeps
 * the first native TLS implementation independent of architectural TP/FS
 * registers while preserving the ELF __tls_get_addr contract used by the
 * general-dynamic model.  Each block is initialized from the PT_TLS image and
 * is intentionally bounded by the fixed thread/object tables. */
static void *tls_address_locked(uintptr_t module, uintptr_t offset) {
    if (module == 0 || !objects) return NULL;
    struct dl_object *object = NULL;
    for (int slot = 0; slot < DL_OBJECT_MAX; ++slot) {
        if (objects[slot].used && objects[slot].tls_module == module) {
            object = &objects[slot];
            break;
        }
    }
    if (!object || object->tls_size == 0 || offset >= object->tls_size) {
        set_error("TLS: invalid module or offset");
        errno = EINVAL;
        return NULL;
    }
    pthread_t thread = pthread_self();
    if (thread == 0) {
        set_error("TLS: no current thread");
        errno = ESRCH;
        return NULL;
    }
    /* Once an image has a PT_TLS block, give both general-dynamic
     * __tls_get_addr callers and static TLS relocations the same per-thread
     * instance.  This keeps dlsym(), compiler-generated GD/TLSDESC access,
     * and initial-exec/local-exec access coherent instead of silently
     * creating two copies of one DSO's TLS data. */
    if (object->static_tls_assigned) {
        unsigned char *address = akaos_static_tls_address(object, offset);
        if (!address) {
            set_error("TLS: static thread block unavailable");
            errno = ENOMEM;
        }
        return address;
    }
    for (size_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
        if (object->tls_thread_ids[slot] == (uint32_t)thread) {
            return (unsigned char *)object->tls_blocks[slot] + offset;
        }
    }
    size_t mapping_length = object->tls_mapping_length;
    if (mapping_length == 0) {
        mapping_length = (object->tls_size + 4095U) & ~((size_t)4095U);
        if (mapping_length == 0) mapping_length = 4096U;
    }
    void *block = mmap(NULL, mapping_length, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED) {
        set_error("TLS: thread block allocation failed");
        errno = ENOMEM;
        return NULL;
    }
    memset(block, 0, mapping_length);
    if (object->tls_template && object->tls_filesz != 0) {
        memcpy(block, object->tls_template, object->tls_filesz);
    }
    for (size_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
        if (object->tls_thread_ids[slot] != 0) continue;
        object->tls_thread_ids[slot] = (uint32_t)thread;
        object->tls_blocks[slot] = block;
        return (unsigned char *)block + offset;
    }
    (void)munmap(block, mapping_length);
    set_error("TLS: thread table exhausted");
    errno = ENOMEM;
    return NULL;
}

void *akaos_dl_tls_address(uintptr_t module, uintptr_t offset) {
    if (!loader_lock()) {
        set_error("TLS: loader mutex unavailable");
        errno = EAGAIN;
        return NULL;
    }
    void *address = tls_address_locked(module, offset);
    loader_unlock();
    return address;
}

#if defined(__aarch64__)
static int register_aarch64_tlsdesc(struct dl_object *object,
                                    uintptr_t descriptor,
                                    uintptr_t module, uintptr_t offset) {
    if (!object || descriptor == 0 || module == 0) return 0;
    for (size_t index = 0; index < object->tlsdesc_count; ++index) {
        if (object->tlsdescs[index].descriptor != descriptor) continue;
        object->tlsdescs[index].module = module;
        object->tlsdescs[index].offset = offset;
        return 1;
    }
    if (object->tlsdesc_count == object->tlsdesc_capacity) {
        size_t capacity = object->tlsdesc_capacity
            ? object->tlsdesc_capacity * 2U : 8U;
        if (capacity < object->tlsdesc_capacity ||
            capacity > SIZE_MAX / sizeof(*object->tlsdescs)) {
            set_error("dlopen: AArch64 TLS descriptor table overflow");
            errno = ENOMEM;
            return 0;
        }
        struct akaos_tlsdesc_entry *entries =
            (struct akaos_tlsdesc_entry *)realloc(
                object->tlsdescs, capacity * sizeof(*entries));
        if (!entries) {
            set_error("dlopen: AArch64 TLS descriptor allocation failed");
            errno = ENOMEM;
            return 0;
        }
        object->tlsdescs = entries;
        object->tlsdesc_capacity = capacity;
    }
    struct akaos_tlsdesc_entry *entry =
        &object->tlsdescs[object->tlsdesc_count++];
    entry->descriptor = descriptor;
    entry->module = module;
    entry->offset = offset;
    return 1;
}

/* AArch64 TLSDESC code adds the resolver's result to TPIDR_EL0.  akaOS keeps
 * TLS blocks in bounded per-thread mappings rather than in an ABI-specific
 * static TLS area, so return the block address relative to the current TP.
 * The subtraction is modulo 2^N and therefore also works when the kernel's
 * TPIDR_EL0 value is zero or is used for another per-thread record. */
__attribute__((used, noinline)) static uintptr_t
akaos_aarch64_tlsdesc_resolver_impl(
    const uintptr_t *descriptor) {
    if (!descriptor || !objects) return 0;
    uintptr_t resolved = 0;
    uintptr_t descriptor_address = (uintptr_t)descriptor;
    for (int slot = 0; slot < DL_OBJECT_MAX && resolved == 0; ++slot) {
        struct dl_object *object = &objects[slot];
        if (!object->used) continue;
        for (size_t index = 0; index < object->tlsdesc_count; ++index) {
            const struct akaos_tlsdesc_entry *entry =
                &object->tlsdescs[index];
            if (entry->descriptor != descriptor_address) continue;
            void *address = akaos_dl_tls_address(entry->module, entry->offset);
            if (address) resolved = (uintptr_t)address;
            break;
        }
    }
    if (!resolved) return 0;
    uintptr_t thread_pointer = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(thread_pointer));
    return resolved - thread_pointer;
}

/* The TLSDESC call sequence is allowed to clobber ordinary argument/result
 * registers but keeps x8 live across the call (Clang uses it to preserve a
 * caller value in TLS-writing functions).  A normal C function may use x8 as
 * a scratch register, so wrap the implementation with a tiny ABI shim that
 * saves and restores x8 while retaining the resolver result in x0. */
__attribute__((naked)) uintptr_t akaos_aarch64_tlsdesc_resolver(
    const uintptr_t *descriptor) {
    __asm__ volatile(
        "stp x8, x30, [sp, #-16]!\n"
        "bl akaos_aarch64_tlsdesc_resolver_impl\n"
        "ldp x8, x30, [sp], #16\n"
        "ret\n");
}
#endif

static int run_object_initializers(struct dl_object *object) {
    if (!object || object->initializers_ran) return 1;

    extern void __akaos_stack_chk_guard_init(void);
    __akaos_stack_chk_guard_init();

    /* Mark before entering foreign code.  A constructor is allowed to call
     * dlopen/dlsym, and recursive lookup must see this object as complete. */
    object->initializers_ran = 1;
    if (object->init) invoke_callback(object->init);
    if (object->init_array) {
        const uintptr_t *entries = (const uintptr_t *)object->init_array;
        for (size_t index = 0; index < object->init_array_count; ++index) {
            uintptr_t callback = entries[index];
            if (callback_in_object(object, callback)) invoke_callback(callback);
        }
    }
    return 1;
}

static void run_object_finalizers(struct dl_object *object) {
    if (!object || object->finalizers_ran) return;
    object->finalizers_ran = 1;
    if (object->fini_array) {
        const uintptr_t *entries = (const uintptr_t *)object->fini_array;
        for (size_t index = object->fini_array_count; index > 0; --index) {
            uintptr_t callback = entries[index - 1];
            if (callback_in_object(object, callback)) invoke_callback(callback);
        }
    }
    if (object->fini) invoke_callback(object->fini);
}

static uint32_t relocation_type(elf_word_t info_lo, elf_word_t info_hi) {
#if AKAOS_ELF64
    (void)info_hi;
    return (uint32_t)info_lo;
#else
    (void)info_hi;
    return (uint32_t)(info_lo & UINT32_C(0xff));
#endif
}

static uint32_t relocation_symbol(elf_word_t info_lo, elf_word_t info_hi) {
#if AKAOS_ELF64
    (void)info_hi;
    return (uint32_t)(info_lo >> 32);
#else
    (void)info_hi;
    return (uint32_t)(info_lo >> 8);
#endif
}

static int is_relative_relocation(uint32_t type) {
#if defined(__x86_64__)
    return type == 8;
#elif defined(__i386__)
    return type == 8;
#elif defined(__aarch64__)
    return type == 1027;
#elif defined(__arm__)
    return type == 23;
#elif defined(__riscv)
    return type == 3;
#else
    (void)type;
    return 0;
#endif
}

static int is_symbol_relocation(uint32_t type) {
#if defined(__x86_64__)
    return type == 1 || type == 6 || type == 7;
#elif defined(__i386__)
    return type == 1 || type == 6 || type == 7;
#elif defined(__aarch64__)
    return type == 257 || type == 1025 || type == 1026;
#elif defined(__arm__)
    return type == 2 || type == 21 || type == 22;
#elif defined(__riscv)
    /* RV64 uses R_RISCV_64 for absolute data pointers while RV32 emits
     * R_RISCV_32 for the same ABI slots (errno, environ, TLS helpers). */
    return type == 1 || type == 2 || type == 5;
#else
    (void)type;
    return 0;
#endif
}

/* Return 1 for a TLS module-id relocation, 2 for a module-relative offset,
 * and 3 for a static-thread-pointer relocation.  The first two forms are the
 * relocations emitted by the general-dynamic model used by PIC DSOs.  A
 * static TP value cannot be represented by akaOS's register-independent TLS
 * backend yet, so it is rejected explicitly rather than being misapplied as
 * an ordinary symbol address. */
static int tls_relocation_kind(uint32_t type) {
#if defined(__x86_64__)
    if (type == 16) return 1; /* R_X86_64_DTPMOD64 */
    if (type == 17) return 2; /* R_X86_64_DTPOFF64 */
    if (type == 18 || type == 22 || type == 23) return 3;
#elif defined(__i386__)
    if (type == 35) return 1; /* R_386_TLS_DTPMOD32 */
    if (type == 36) return 2; /* R_386_TLS_DTPOFF32 */
    if (type == 37 || type == 14 || type == 15) return 3;
#elif defined(__aarch64__)
    if (type == 1028) return 1; /* R_AARCH64_TLS_DTPMOD64 */
    if (type == 1029) return 2; /* R_AARCH64_TLS_DTPREL64 */
    if (type == 1031) return 4; /* R_AARCH64_TLSDESC */
    if (type == 1030 || type == 1032) return 3;
#elif defined(__arm__)
    if (type == 17) return 1; /* R_ARM_TLS_DTPMOD32 */
    if (type == 18) return 2; /* R_ARM_TLS_DTPOFF32 */
    if (type == 19 || (type >= 104 && type <= 111)) return 3;
#elif defined(__riscv)
    if (type == 6 || type == 7) return 1; /* R_RISCV_TLS_DTPMOD{32,64} */
    if (type == 8 || type == 9) return 2; /* R_RISCV_TLS_DTPREL{32,64} */
    if (type == 10 || type == 11) return 3; /* R_RISCV_TLS_TPREL{32,64} */
#else
    (void)type;
#endif
    return 0;
}

static int is_jump_slot_relocation(uint32_t type) {
#if defined(__x86_64__) || defined(__i386__)
    return type == 7;
#elif defined(__aarch64__)
    return type == 1026;
#elif defined(__arm__)
    return type == 22;
#elif defined(__riscv)
    return type == 5;
#else
    (void)type;
    return 0;
#endif
}

static int symbol_is_tls(const struct elf_sym *symbol) {
    return symbol && (symbol->info & 0x0fU) == STT_TLS;
}

static int symbol_value(const struct dl_object *object, uint32_t index,
                        uintptr_t *value) {
    if (!object->symtab || index >= object->symbol_count) return 0;
    const struct elf_sym *symbol = &object->symtab[index];
    if (symbol->shndx == SHN_UNDEF) return 0;
    if (symbol_is_tls(symbol)) {
        void *address = object->static_tls_assigned ?
            (void *)akaos_static_tls_address(object, (uintptr_t)symbol->value) :
            akaos_dl_tls_address(object->tls_module,
                                 (uintptr_t)symbol->value);
        if (!address) return 0;
        *value = (uintptr_t)address;
        return 1;
    }
    *value = mapped_address(object, (uint64_t)symbol->value);
    return 1;
}

static const char *object_symbol_name(const struct dl_object *object,
                                      uint32_t index) {
    if (!object || !object->symtab || index >= object->symbol_count ||
        !object->strtab || object->strtab_size == 0) return NULL;
    const struct elf_sym *symbol = &object->symtab[index];
    if (symbol->name >= object->strtab_size) return NULL;
    const char *name = object->strtab + symbol->name;
    size_t remaining = object->strtab_size - symbol->name;
    return memchr(name, '\0', remaining) ? name : NULL;
}

static const char *object_string_at(const struct dl_object *object,
                                    uint32_t offset) {
    if (!object || !object->strtab || offset >= object->strtab_size) return NULL;
    const char *name = object->strtab + offset;
    size_t remaining = object->strtab_size - (size_t)offset;
    return memchr(name, '\0', remaining) ? name : NULL;
}

static int version_record_next(uint64_t base, uint32_t next,
                               uint64_t *cursor) {
    if (!cursor || next == 0 || base > UINT64_MAX - next) return 0;
    *cursor = base + next;
    return 1;
}

/* Resolve a version index from the requestor's DT_VERNEED chain.  A NULL
 * result means the symbol has no version requirement (the normal unversioned
 * lookup path).  `valid` distinguishes that case from a malformed or
 * unsatisfied version record so relocation cannot silently fall back to an
 * incompatible provider. */
static const char *requested_symbol_version(const struct dl_object *object,
                                            uint32_t symbol_index,
                                            int *valid) {
    if (valid) *valid = 1;
    if (!object || !object->versym || symbol_index >= object->symbol_count ||
        object->verneed_vaddr == 0 || object->verneed_count == 0) return NULL;
    uint16_t raw = 0;
    memcpy(&raw, &object->versym[symbol_index], sizeof(raw));
    uint16_t version_index = raw & AKAOS_VERSYM_INDEX_MASK;
    if (version_index <= 1) return NULL;
    if (object->verneed_count > AKAOS_VERSION_MAX) {
        if (valid) *valid = 0;
        return NULL;
    }
    uint64_t cursor = object->verneed_vaddr;
    for (uint32_t need_index = 0; need_index < object->verneed_count;
         ++need_index) {
        if (!mapping_range_ok(object, cursor, sizeof(struct elf_verneed))) {
            if (valid) *valid = 0;
            return NULL;
        }
        struct elf_verneed need;
        memcpy(&need, (const void *)mapped_address(object, cursor), sizeof(need));
        if (need.aux == 0 || cursor > UINT64_MAX - need.aux) {
            if (valid) *valid = 0;
            return NULL;
        }
        uint64_t aux_cursor = cursor + need.aux;
        uint32_t aux_count = need.count;
        if (aux_count > AKAOS_VERSION_MAX) {
            if (valid) *valid = 0;
            return NULL;
        }
        for (uint32_t aux_index = 0; aux_index < aux_count; ++aux_index) {
            if (!mapping_range_ok(object, aux_cursor,
                                  sizeof(struct elf_vernaux))) {
                if (valid) *valid = 0;
                return NULL;
            }
            struct elf_vernaux aux;
            memcpy(&aux, (const void *)mapped_address(object, aux_cursor),
                   sizeof(aux));
            if (aux.other == version_index) {
                const char *version = object_string_at(object, aux.name);
                if (!version && valid) *valid = 0;
                return version;
            }
            if (aux.next == 0) break;
            if (!version_record_next(aux_cursor, aux.next, &aux_cursor)) {
                if (valid) *valid = 0;
                return NULL;
            }
        }
        if (need.next == 0) break;
        if (!version_record_next(cursor, need.next, &cursor)) {
            if (valid) *valid = 0;
            return NULL;
        }
    }
    if (valid) *valid = 0;
    return NULL;
}

/* Resolve a provider's version definition for one dynamic symbol.  The
 * hidden bit is returned separately because dlsym without an explicit
 * version must select the provider's default (non-hidden) definition. */
static int provider_symbol_version(const struct dl_object *object,
                                   uint32_t symbol_index,
                                   const char **name, int *hidden) {
    if (name) *name = NULL;
    if (hidden) *hidden = 0;
    if (!object || !object->versym || symbol_index >= object->symbol_count)
        return 1;
    uint16_t raw = 0;
    memcpy(&raw, &object->versym[symbol_index], sizeof(raw));
    uint16_t version_index = raw & AKAOS_VERSYM_INDEX_MASK;
    if (hidden) *hidden = (raw & AKAOS_VERSYM_HIDDEN) != 0;
    if (version_index <= 1) return 1;
    if (object->verdef_vaddr == 0 || object->verdef_count == 0 ||
        object->verdef_count > AKAOS_VERSION_MAX) return 0;
    uint64_t cursor = object->verdef_vaddr;
    for (uint32_t definition = 0; definition < object->verdef_count;
         ++definition) {
        if (!mapping_range_ok(object, cursor, sizeof(struct elf_verdef)))
            return 0;
        struct elf_verdef def;
        memcpy(&def, (const void *)mapped_address(object, cursor), sizeof(def));
        if (def.index == version_index) {
            if (def.aux == 0 || cursor > UINT64_MAX - def.aux) return 0;
            uint64_t aux_cursor = cursor + def.aux;
            if (!mapping_range_ok(object, aux_cursor,
                                  sizeof(struct elf_verdaux))) return 0;
            struct elf_verdaux aux;
            memcpy(&aux, (const void *)mapped_address(object, aux_cursor),
                   sizeof(aux));
            const char *version_name = object_string_at(object, aux.name);
            if (!version_name) return 0;
            if (name) *name = version_name;
            return 1;
        }
        if (def.next == 0) break;
        if (!version_record_next(cursor, def.next, &cursor)) return 0;
    }
    return 0;
}

static int provider_symbol_matches(const struct dl_object *object,
                                   uint32_t symbol_index,
                                   const char *requested_version) {
    const char *provided_version = NULL;
    int hidden = 0;
    if (!provider_symbol_version(object, symbol_index, &provided_version,
                                 &hidden)) return 0;
    if (requested_version && requested_version[0] != '\0') {
        if (!provided_version) {
            /* The provider does not version this symbol, so its default
             * definition is the one the requester asks for.  Upstream
             * libraries built against glibc require symbols such as
             * memcpy@GLIBC_2.14, and a provider without a version table
             * still satisfies that requirement. */
            return !hidden;
        }
        return strcmp(provided_version, requested_version) == 0;
    }
    /* An unversioned lookup uses the default provider definition.  A DSO
     * without any version table, or a global/unversioned symbol, is valid. */
    return !hidden;
}

static uint32_t gnu_symbol_hash(const char *name) {
    uint32_t hash = 5381U;
    const unsigned char *byte = (const unsigned char *)name;
    while (*byte) hash = hash * 33U + *byte++;
    return hash;
}

/* Use DT_GNU_HASH's bloom filter and bucket chains for the relocation hot
 * path. Qt and other desktop libraries have thousands of dynamic symbols; a
 * full dynsym scan for every imported name made their first load effectively
 * quadratic. Return -1 only for objects without GNU hash so legacy objects
 * retain the conservative linear fallback. */
static int gnu_hash_symbol_value(const struct dl_object *object,
                                 const char *name, const char *version,
                                 uintptr_t *value) {
    if (!object || !object->gnu_hash_vaddr) return -1;
    const uint64_t hash_vaddr = object->gnu_hash_vaddr;
    if (!mapping_range_ok(object, hash_vaddr, 16U)) return 0;

    const unsigned char *header = (const unsigned char *)
        mapped_address(object, hash_vaddr);
    uint32_t bucket_count, symbol_offset, bloom_count, bloom_shift;
    memcpy(&bucket_count, header + 0U, sizeof(bucket_count));
    memcpy(&symbol_offset, header + 4U, sizeof(symbol_offset));
    memcpy(&bloom_count, header + 8U, sizeof(bloom_count));
    memcpy(&bloom_shift, header + 12U, sizeof(bloom_shift));
    if (!bucket_count || !bloom_count || bloom_shift >= 32U) return 0;

    uint64_t bloom_bytes = (uint64_t)bloom_count * sizeof(uintptr_t);
    uint64_t bucket_offset = 16U + bloom_bytes;
    uint64_t bucket_bytes = (uint64_t)bucket_count * sizeof(uint32_t);
    if (bucket_offset < 16U || bucket_bytes > UINT64_MAX - bucket_offset)
        return 0;
    uint64_t chain_offset = bucket_offset + bucket_bytes;
    if (!mapping_range_ok(object, hash_vaddr, chain_offset)) return 0;

    uint32_t hash = gnu_symbol_hash(name);
    const unsigned word_bits = (unsigned)(sizeof(uintptr_t) * 8U);
    uint64_t bloom_entry = hash_vaddr + 16U +
        ((uint64_t)(hash / word_bits) % bloom_count) * sizeof(uintptr_t);
    uintptr_t bloom_word = 0;
    if (!mapping_range_ok(object, bloom_entry, sizeof(bloom_word))) return 0;
    memcpy(&bloom_word, (const void *)mapped_address(object, bloom_entry),
           sizeof(bloom_word));
    uintptr_t bloom_mask = ((uintptr_t)1U << (hash % word_bits)) |
        ((uintptr_t)1U << ((hash >> bloom_shift) % word_bits));
    if ((bloom_word & bloom_mask) != bloom_mask) return 0;

    uint64_t bucket_entry = hash_vaddr + bucket_offset +
        (uint64_t)(hash % bucket_count) * sizeof(uint32_t);
    uint32_t symbol_index = 0U;
    if (!mapping_range_ok(object, bucket_entry, sizeof(symbol_index))) return 0;
    memcpy(&symbol_index, (const void *)mapped_address(object, bucket_entry),
           sizeof(symbol_index));
    if (symbol_index < symbol_offset || symbol_index >= object->symbol_count)
        return 0;

    for (;;) {
        uint64_t chain_entry = hash_vaddr + chain_offset +
            (uint64_t)(symbol_index - symbol_offset) * sizeof(uint32_t);
        uint32_t chain_hash = 0U;
        if (chain_entry < hash_vaddr ||
            !mapping_range_ok(object, chain_entry, sizeof(chain_hash)))
            return 0;
        memcpy(&chain_hash, (const void *)mapped_address(object, chain_entry),
               sizeof(chain_hash));
        if ((chain_hash | 1U) == (hash | 1U)) {
            const struct elf_sym *symbol = &object->symtab[symbol_index];
            const char *symbol_name =
                object_symbol_name(object, symbol_index);
            if (symbol_name && symbol->shndx != SHN_UNDEF &&
                strcmp(symbol_name, name) == 0 &&
                provider_symbol_matches(object, symbol_index, version))
                return symbol_value(object, symbol_index, value);
        }
        if (chain_hash & 1U) break;
        if (++symbol_index >= object->symbol_count) return 0;
    }
    return 0;
}

static int symbol_value_by_name(const struct dl_object *object,
                                const char *name, const char *version,
                                uintptr_t *value) {
    if (!object || !object->used || object->loading || !name || !value ||
        !object->symtab) return 0;
    int hashed = gnu_hash_symbol_value(object, name, version, value);
    if (hashed >= 0) return hashed;
    for (size_t index = 0; index < object->symbol_count; ++index) {
        const struct elf_sym *symbol = &object->symtab[index];
        const char *symbol_name = object_symbol_name(object, (uint32_t)index);
        if (!symbol_name || symbol->shndx == SHN_UNDEF ||
            strcmp(symbol_name, name) != 0 ||
            !provider_symbol_matches(object, (uint32_t)index, version)) continue;
        /* Resolve through the same path used by relocations.  In particular,
         * STT_TLS symbols must return the caller's per-thread block rather
         * than the symbol's link-time offset inside the DSO image. */
        return symbol_value(object, (uint32_t)index, value);
    }
    return 0;
}

static int tls_symbol_by_name(const struct dl_object *object, const char *name,
                              const char *version, uintptr_t *module,
                              uintptr_t *offset) {
    if (!object || !object->used || object->loading || !name || !module ||
        !offset || !object->symtab || object->tls_module == 0) return 0;
    for (size_t index = 0; index < object->symbol_count; ++index) {
        const struct elf_sym *symbol = &object->symtab[index];
        const char *symbol_name = object_symbol_name(object, (uint32_t)index);
        if (!symbol_name || symbol->shndx == SHN_UNDEF ||
            !symbol_is_tls(symbol) || strcmp(symbol_name, name) != 0 ||
            !provider_symbol_matches(object, (uint32_t)index, version)) continue;
        *module = object->tls_module;
        *offset = (uintptr_t)symbol->value;
        return 1;
    }
    return 0;
}

static int resolve_tls_symbol(const struct dl_object *requestor, uint32_t index,
                              uintptr_t *module, uintptr_t *offset) {
    if (!requestor || !requestor->symtab || index >= requestor->symbol_count ||
        !module || !offset || !objects) return 0;
    const struct elf_sym *symbol = &requestor->symtab[index];
    if (symbol->shndx != SHN_UNDEF) {
        if (!symbol_is_tls(symbol) || requestor->tls_module == 0) return 0;
        *module = requestor->tls_module;
        *offset = (uintptr_t)symbol->value;
        return 1;
    }
    const char *name = object_symbol_name(requestor, index);
    if (!name || !name[0]) return 0;
    int version_valid = 1;
    const char *requested_version =
        requested_symbol_version(requestor, index, &version_valid);
    if (!version_valid) {
        set_error("dlopen: unsatisfied symbol version");
        return 0;
    }
    for (uint32_t dependency = 0; dependency < requestor->dependency_count;
         ++dependency) {
        if (tls_symbol_by_name(requestor->dependencies[dependency], name,
                               requested_version, module, offset)) return 1;
    }
    for (int slot = 0; slot < DL_OBJECT_MAX; ++slot) {
        if (&objects[slot] == requestor || !objects[slot].global_scope)
            continue;
        if (tls_symbol_by_name(&objects[slot], name, requested_version,
                               module, offset)) return 1;
    }
    return 0;
}

static int static_tls_symbol_by_name(const struct dl_object *object,
                                     const char *name, const char *version,
                                     intptr_t *offset) {
    /* Static TLS relocations are resolved while the object is still marked
     * loading.  Unlike ordinary function/data symbol lookup, this path only
     * needs the already-parsed local PT_TLS template (or a fully loaded
     * dependency), so rejecting a loading requestor makes every local
     * initial-exec relocation look unresolved. */
    if (!object || !object->used || !name || !offset ||
        !object->static_tls_assigned || !object->symtab) return 0;
    for (size_t index = 0; index < object->symbol_count; ++index) {
        const struct elf_sym *symbol = &object->symtab[index];
        const char *symbol_name = object_symbol_name(object, (uint32_t)index);
        if (!symbol_name || symbol->shndx == SHN_UNDEF ||
            !symbol_is_tls(symbol) || strcmp(symbol_name, name) != 0 ||
            !provider_symbol_matches(object, (uint32_t)index, version)) continue;
        if ((uintptr_t)symbol->value >= object->tls_size) return 0;
        *offset = object->static_tls_offset + (intptr_t)symbol->value;
        return 1;
    }
    return 0;
}

static int resolve_static_tls_symbol(const struct dl_object *requestor,
                                     uint32_t index, intptr_t *offset) {
    if (!requestor || !requestor->symtab || index >= requestor->symbol_count ||
        !offset || !objects) return 0;
    const struct elf_sym *symbol = &requestor->symtab[index];
    const char *name = object_symbol_name(requestor, index);
    if (!name || !name[0]) return 0;
    int version_valid = 1;
    const char *requested_version =
        requested_symbol_version(requestor, index, &version_valid);
    if (!version_valid) {
        set_error("dlopen: unsatisfied symbol version");
        return 0;
    }
    if (symbol->shndx != SHN_UNDEF)
        return static_tls_symbol_by_name(requestor, name, requested_version,
                                         offset);
    for (uint32_t dependency = 0; dependency < requestor->dependency_count;
         ++dependency) {
        if (static_tls_symbol_by_name(requestor->dependencies[dependency], name,
                                      requested_version, offset)) return 1;
    }
    for (int slot = 0; slot < DL_OBJECT_MAX; ++slot) {
        if (&objects[slot] == requestor || !objects[slot].global_scope)
            continue;
        if (static_tls_symbol_by_name(&objects[slot], name,
                                      requested_version, offset)) return 1;
    }
    return 0;
}

static uintptr_t add_relocation_addend(uintptr_t value, int64_t addend) {
    if (addend >= 0) return value + (uintptr_t)addend;
    return value - (uintptr_t)(-(addend + 1)) - (uintptr_t)1;
}

/* Resolve the module-id and module-relative offset words used by a
 * general-dynamic TLS descriptor.  A return value of 1 means the relocation
 * was handled, -1 means it was recognized but invalid/unsupported, and 0
 * means it is an ordinary non-TLS relocation. */
static int resolve_tls_relocation(struct dl_object *object,
                                  uint32_t index, uint32_t type,
                                  int64_t addend, uintptr_t target,
                                  uintptr_t *value) {
    int kind = tls_relocation_kind(type);
    if (kind == 0) return 0;
    if (kind == 3) {
        intptr_t static_offset = 0;
        if (!resolve_static_tls_symbol(object, index, &static_offset)) {
            set_error("dlopen: unresolved static TLS relocation");
            return -1;
        }
        *value = add_relocation_addend((uintptr_t)static_offset, addend);
        return 1;
    }
    uintptr_t module = 0;
    uintptr_t offset = 0;
    /* A zero-symbol DTPMOD/D TPOFF relocation names the TLS module containing
     * the relocation itself. Local TLS variables commonly use this form and
     * do not have a dynsym entry to resolve. */
    if (index == 0 && object->tls_module != 0) {
        module = object->tls_module;
        offset = 0;
    } else if (!resolve_tls_symbol(object, index, &module, &offset)) {
        set_error("dlopen: unresolved TLS relocation");
        return -1;
    }
#if defined(__aarch64__)
    if (kind == 4) {
        if (!register_aarch64_tlsdesc(object, target, module, offset)) return -1;
        *value = (uintptr_t)&akaos_aarch64_tlsdesc_resolver;
        return 1;
    }
#else
    (void)target;
#endif
    uintptr_t relocation_value =
        add_relocation_addend(kind == 1 ? module : offset, addend);
#if defined(__riscv)
    /* R_RISCV_TLS_DTPREL relocations carry S + A - TLS_DTV_OFFSET. The
     * architecture's __tls_get_addr shim adds TLS_DTV_OFFSET back before
     * passing the module-relative offset to the native TLS allocator. */
    if (kind == 2) relocation_value -= (uintptr_t)0x800U;
#endif
    *value = relocation_value;
    return 1;
}

/* GNU-hash-only objects do not carry the older SYSV DT_HASH table.  Derive
 * the dynamic symbol count from each bucket's chain, with every pointer
 * arithmetic step bounded by the mapped image.  This keeps dlsym and DT_NEEDED
 * resolution compatible with modern toolchains without trusting file offsets
 * supplied by an untrusted ELF image. */
static int gnu_hash_symbol_count(const struct dl_object *object,
                                 uint64_t hash_vaddr, size_t *count) {
    if (!object || !count || !mapping_range_ok(object, hash_vaddr, 16)) return 0;
    const unsigned char *base = (const unsigned char *)mapped_address(object, hash_vaddr);
    uint32_t bucket_count, symbol_offset, bloom_count, bloom_shift;
    memcpy(&bucket_count, base + 0, sizeof(bucket_count));
    memcpy(&symbol_offset, base + 4, sizeof(symbol_offset));
    memcpy(&bloom_count, base + 8, sizeof(bloom_count));
    memcpy(&bloom_shift, base + 12, sizeof(bloom_shift));
    (void)bloom_shift;
    if (bucket_count == 0 || bloom_count == 0 || symbol_offset > UINT32_MAX - 1U)
        return 0;
#if AKAOS_ELF64
    const uint64_t bloom_bytes = (uint64_t)bloom_count * sizeof(uint64_t);
#else
    const uint64_t bloom_bytes = (uint64_t)bloom_count * sizeof(uint32_t);
#endif
    const uint64_t bucket_offset = 16U + bloom_bytes;
    const uint64_t chain_offset = bucket_offset + (uint64_t)bucket_count * sizeof(uint32_t);
    if (chain_offset < bucket_offset ||
        !mapping_range_ok(object, hash_vaddr, chain_offset)) return 0;
    size_t highest = (size_t)symbol_offset;
    for (uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
        uint32_t first;
        uint64_t bucket_vaddr = hash_vaddr + bucket_offset +
                                (uint64_t)bucket * sizeof(uint32_t);
        if (!mapping_range_ok(object, bucket_vaddr, sizeof(first))) return 0;
        memcpy(&first, (const void *)mapped_address(object, bucket_vaddr), sizeof(first));
        if (first < symbol_offset) continue;
        uint32_t index = first;
        for (;;) {
            uint64_t chain_vaddr = hash_vaddr + chain_offset +
                                   (uint64_t)(index - symbol_offset) * sizeof(uint32_t);
            uint32_t chain;
            if (chain_vaddr < hash_vaddr ||
                !mapping_range_ok(object, chain_vaddr, sizeof(chain))) return 0;
            memcpy(&chain, (const void *)mapped_address(object, chain_vaddr), sizeof(chain));
            if ((size_t)index + 1U > highest) highest = (size_t)index + 1U;
            if (chain & 1U) break;
            if (index == UINT32_MAX) return 0;
            ++index;
        }
    }
    if (highest == 0 || highest > object->mapping_length / sizeof(struct elf_sym) +
                             (size_t)symbol_offset + 1U) return 0;
    *count = highest;
    return 1;
}

static int resolve_symbol(const struct dl_object *requestor, uint32_t index,
                          uintptr_t *value) {
    if (!requestor || !value || !objects) return 0;
    const struct elf_sym *symbol = requestor->symtab &&
        index < requestor->symbol_count ? &requestor->symtab[index] : NULL;
    if (!symbol) return 0;
    if (symbol->shndx != SHN_UNDEF) {
        return symbol_value(requestor, index, value);
    }
    const char *name = object_symbol_name(requestor, index);
    if (!name || !name[0]) return 0;
    if (loader_trace_enabled()) {
        printf("loader: resolving %s for %s\n", name,
               requestor->path ? requestor->path : "(unknown)");
        fflush(stdout);
    }
    int version_valid = 1;
    const char *requested_version =
        requested_symbol_version(requestor, index, &version_valid);
    if (!version_valid) return 0;
    /* __tls_get_addr is part of akaOS libc itself.  It is intentionally
     * resolved as a built-in ABI entry rather than requiring every ET_DYN
     * executable to export its whole static libc symbol table. */
    if (strcmp(name, "__tls_get_addr") == 0 ||
        strcmp(name, "___tls_get_addr") == 0) {
        *value = (uintptr_t)&__tls_get_addr;
        return 1;
    }
#if defined(__arm__)
    if (strcmp(name, "__aeabi_read_tp") == 0) {
        *value = (uintptr_t)&__aeabi_read_tp;
        return 1;
    }
#endif
    for (uint32_t dep = 0; dep < requestor->dependency_count; ++dep) {
        if (symbol_value_by_name(requestor->dependencies[dep], name,
                                 requested_version, value)) return 1;
    }
    for (int slot = 0; slot < DL_OBJECT_MAX; ++slot) {
        if (&objects[slot] == requestor || !objects[slot].global_scope)
            continue;
        if (symbol_value_by_name(&objects[slot], name, requested_version,
                                 value)) return 1;
    }
    /* ELF permits an undefined weak reference to remain unresolved. Its
     * address is defined as zero; rejecting it prevents otherwise valid DSOs
     * from loading when an optional hook (for example __register_frame) is
     * intentionally absent from the process scope. */
    if ((symbol->info >> 4) == STB_WEAK) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int apply_relocations(struct dl_object *object, const struct elf_dyn *dynamic,
                             size_t dynamic_count) {
    uint64_t rela = 0, rela_size = 0, rela_ent = sizeof(struct elf_rela);
    uint64_t rel = 0, rel_size = 0, rel_ent = sizeof(struct elf_rel);
    uint64_t jmprel = 0, jmprel_size = 0, pltrel = DT_RELA;
    for (size_t i = 0; i < dynamic_count; ++i) {
        int64_t tag = (int64_t)dynamic[i].tag;
        uint64_t value = (uint64_t)dynamic[i].value;
        if (tag == DT_RELA) rela = value;
        else if (tag == DT_RELASZ) rela_size = value;
        else if (tag == DT_RELAENT) rela_ent = value;
        else if (tag == DT_REL) rel = value;
        else if (tag == DT_RELSZ) rel_size = value;
        else if (tag == DT_RELENT) rel_ent = value;
        else if (tag == 23) jmprel = value;
        else if (tag == 2) jmprel_size = value;
        else if (tag == 20) pltrel = value;
    }
    int rela_aligned;
#if AKAOS_ELF64
    rela_aligned = !rela || (rela_ent >= sizeof(struct elf_rela) && rela_size % rela_ent == 0);
#else
    rela_aligned = !rela || ((uint32_t)rela_ent >= sizeof(struct elf_rela) &&
                             (uint32_t)rela_size % (uint32_t)rela_ent == 0);
#endif
    if (!rela_aligned) {
        set_error("dlopen: malformed RELA table");
        return 0;
    }
    int rel_aligned;
#if AKAOS_ELF64
    rel_aligned = !rel || (rel_ent >= sizeof(struct elf_rel) && rel_size % rel_ent == 0);
#else
    rel_aligned = !rel || ((uint32_t)rel_ent >= sizeof(struct elf_rel) &&
                           (uint32_t)rel_size % (uint32_t)rel_ent == 0);
#endif
    if (!rel_aligned) {
        set_error("dlopen: malformed REL table");
        return 0;
    }
    for (uint64_t offset = 0; offset < rela_size; offset += rela_ent) {
        const struct elf_rela *entry = (const struct elf_rela *)
            (mapped_address(object, rela) + (uintptr_t)offset);
#if AKAOS_ELF64
        elf_word_t info_lo = (elf_word_t)entry->info;
        elf_word_t info_hi = (elf_word_t)(entry->info >> 32);
#else
        elf_word_t info_lo = entry->info_lo;
        elf_word_t info_hi = 0;
#endif
        uint32_t type = relocation_type(info_lo, info_hi);
        uint32_t index = relocation_symbol(info_lo, info_hi);
        uintptr_t target = mapped_address(object, (uint64_t)entry->offset);
        uintptr_t value = 0;
        if (!range_ok(object->mapping_length, (uint64_t)(target - (uintptr_t)object->mapping), sizeof(uintptr_t))) {
            set_error("dlopen: relocation target outside image");
            return 0;
        }
        int tls_result = resolve_tls_relocation(object, index, type,
                                                 (int64_t)entry->addend,
                                                 target,
                                                 &value);
        if (is_relative_relocation(type)) value = add_relocation_addend(object->load_bias,
                                                                         (int64_t)entry->addend);
        else if (tls_result > 0) {
            /* value was produced by the TLS descriptor resolver. */
        } else if (tls_result < 0) return 0;
        else if (is_symbol_relocation(type) && resolve_symbol(object, index, &value)) {
            value += (uintptr_t)entry->addend;
        } else {
            const char *sym_name = object_symbol_name(object, index);
            printf("akaos ld: unresolved relocation for symbol '%s' (type %u, index %u) in %s\n",
                   sym_name ? sym_name : "(null)", type, index, object->path ? object->path : "(unknown)");
            fflush(stdout);
            set_error("dlopen: unsupported or unresolved relocation");
            return 0;
        }
        *(uintptr_t *)target = value;
    }
    for (uint64_t offset = 0; offset < rel_size; offset += rel_ent) {
        const struct elf_rel *entry = (const struct elf_rel *)
            (mapped_address(object, rel) + (uintptr_t)offset);
#if AKAOS_ELF64
        elf_word_t info_lo = (elf_word_t)entry->info;
        elf_word_t info_hi = (elf_word_t)(entry->info >> 32);
#else
        elf_word_t info_lo = entry->info_lo;
        elf_word_t info_hi = 0;
#endif
        uint32_t type = relocation_type(info_lo, info_hi);
        uint32_t index = relocation_symbol(info_lo, info_hi);
        uintptr_t target = mapped_address(object, (uint64_t)entry->offset);
        uintptr_t value = 0;
        if (!range_ok(object->mapping_length, (uint64_t)(target - (uintptr_t)object->mapping), sizeof(uintptr_t))) {
            set_error("dlopen: relocation target outside image");
            return 0;
        }
        int64_t rel_addend = (int64_t)*(uintptr_t *)target;
        int tls_result = resolve_tls_relocation(object, index, type,
                                                 rel_addend, target, &value);
        if (is_relative_relocation(type)) value = object->load_bias +
                                                       (uintptr_t)rel_addend;
        else if (tls_result > 0) {
            /* value was produced by the TLS descriptor resolver. */
        } else if (tls_result < 0) return 0;
        else if (is_symbol_relocation(type) && resolve_symbol(object, index, &value)) {
            /* REL entries carry their addend in the target word. */
            value += (uintptr_t)rel_addend;
        } else {
            set_error("dlopen: unsupported or unresolved relocation");
            return 0;
        }
        *(uintptr_t *)target = value;
    }
    if (jmprel && jmprel_size) {
        if (pltrel == DT_RELA) {
#if AKAOS_ELF64
            int malformed = jmprel_size % rela_ent != 0;
#else
            int malformed = (uint32_t)jmprel_size % (uint32_t)rela_ent != 0;
#endif
            if (malformed || rela_ent < sizeof(struct elf_rela)) {
                set_error("dlopen: malformed PLT RELA table");
                return 0;
            }
            for (uint64_t offset = 0; offset < jmprel_size; offset += rela_ent) {
                const struct elf_rela *entry = (const struct elf_rela *)
                    (mapped_address(object, jmprel) + (uintptr_t)offset);
#if AKAOS_ELF64
                elf_word_t info_lo = (elf_word_t)entry->info;
                elf_word_t info_hi = (elf_word_t)(entry->info >> 32);
#else
                elf_word_t info_lo = entry->info_lo;
                elf_word_t info_hi = 0;
#endif
                uint32_t type = relocation_type(info_lo, info_hi);
                uint32_t index = relocation_symbol(info_lo, info_hi);
                uintptr_t target = mapped_address(object, (uint64_t)entry->offset);
                uintptr_t value = 0;
                if (!mapping_range_ok(object, (uint64_t)entry->offset,
                                      sizeof(uintptr_t))) {
                    set_error("dlopen: PLT target outside image");
                    return 0;
                }
                if (is_relative_relocation(type)) value = object->load_bias + (uintptr_t)entry->addend;
                else if (is_symbol_relocation(type) && resolve_symbol(object, index, &value)) value += (uintptr_t)entry->addend;
                else {
                    const char *name = object_symbol_name(object, index);
                    char detail[DL_ERROR_MAX];
                    (void)snprintf(detail, sizeof(detail),
                                   "dlopen: unresolved PLT relocation %s in %s",
                                   name ? name : "(unknown)", object->path);
                    set_error(detail);
                    return 0;
                }
                *(uintptr_t *)target = value;
            }
        } else if (pltrel == DT_REL) {
#if AKAOS_ELF64
            int malformed = jmprel_size % rel_ent != 0;
#else
            int malformed = (uint32_t)jmprel_size % (uint32_t)rel_ent != 0;
#endif
            if (malformed || rel_ent < sizeof(struct elf_rel)) {
                set_error("dlopen: malformed PLT REL table");
                return 0;
            }
            for (uint64_t offset = 0; offset < jmprel_size; offset += rel_ent) {
                const struct elf_rel *entry = (const struct elf_rel *)
                    (mapped_address(object, jmprel) + (uintptr_t)offset);
#if AKAOS_ELF64
                elf_word_t info_lo = (elf_word_t)entry->info;
                elf_word_t info_hi = (elf_word_t)(entry->info >> 32);
#else
                elf_word_t info_lo = entry->info_lo;
                elf_word_t info_hi = 0;
#endif
                uint32_t type = relocation_type(info_lo, info_hi);
                uint32_t index = relocation_symbol(info_lo, info_hi);
                uintptr_t target = mapped_address(object, (uint64_t)entry->offset);
                uintptr_t value = 0;
                if (!mapping_range_ok(object, (uint64_t)entry->offset,
                                      sizeof(uintptr_t))) {
                    set_error("dlopen: PLT target outside image");
                    return 0;
                }
                if (is_relative_relocation(type)) value = object->load_bias + *(uintptr_t *)target;
                else if (is_symbol_relocation(type) && resolve_symbol(object, index, &value)) {
                    /* JUMP_SLOT entries use the existing word for lazy binding;
                     * RTLD_NOW resolves them directly and must not add the PLT
                     * stub address that the linker placed there. */
                    if (!is_jump_slot_relocation(type)) value += *(uintptr_t *)target;
                }
                else {
                    const char *name = object_symbol_name(object, index);
                    char detail[DL_ERROR_MAX];
                    (void)snprintf(detail, sizeof(detail),
                                   "dlopen: unresolved PLT relocation %s in %s",
                                   name ? name : "(unknown)", object->path);
                    set_error(detail);
                    return 0;
                }
                *(uintptr_t *)target = value;
            }
        } else {
            set_error("dlopen: unknown PLT relocation format");
            return 0;
        }
    }
    return 1;
}

static int machine_matches(elf_half_t machine) {
#if defined(__x86_64__)
    return machine == 62;
#elif defined(__i386__)
    return machine == 3;
#elif defined(__aarch64__)
    return machine == 183;
#elif defined(__arm__)
    return machine == 40;
#elif defined(__riscv)
    return machine == 243;
#else
    (void)machine;
    return 0;
#endif
}

static int loader_read_exact(int descriptor, void *buffer, size_t length) {
    unsigned char *destination = (unsigned char *)buffer;
    size_t received = 0;
    while (received < length) {
        ssize_t count = read(descriptor, destination + received,
                             length - received);
        if (count <= 0) return 0;
        received += (size_t)count;
    }
    return 1;
}

/* Static executables use linker-resolved TPREL/TPOFF relocations rather than
 * passing through dlopen. Read their PT_TLS template before application code
 * starts, reserve slot 1 (the ELF main module), and install a first thread TLS
 * block. Dynamic programs are initialized by the ELF interpreter's dlopen
 * path and do not call this routine. */
int akaos_dl_main_tls_initialize(const char *path,
                                 uintptr_t program_header_address,
                                 uintptr_t program_header_count,
                                 uintptr_t program_header_entry_size) {
    int saved_errno = errno;
    int descriptor = -1;
    struct elf_phdr *programs = NULL;
    unsigned char *tls_template = NULL;
    int success = 0;

    if (!path || !*path) goto done;
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) goto done;

    struct akaos_stat status;
    struct elf_ehdr header;
    if (stat(path, &status) < 0 ||
        status.size < sizeof(header) ||
        (uint64_t)status.size > SIZE_MAX ||
        !loader_read_exact(descriptor, &header, sizeof(header)) ||
        header.ident[0] != 0x7f || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != ELF_CLASS || header.ident[5] != ELFDATA2LSB ||
        (header.type != ET_EXEC && header.type != ET_DYN) ||
        !machine_matches(header.machine) ||
        header.phentsize != sizeof(struct elf_phdr) ||
        header.phnum == 0 || header.phnum > 128U ||
        program_header_address == 0U ||
        program_header_entry_size != sizeof(struct elf_phdr) ||
        program_header_count != header.phnum)
        goto done;

    uint64_t program_bytes =
        (uint64_t)header.phnum * sizeof(struct elf_phdr);
    if (!range_ok((size_t)status.size, (uint64_t)header.phoff,
                  program_bytes) ||
        (uint64_t)(off_t)header.phoff != (uint64_t)header.phoff ||
        lseek(descriptor, (off_t)header.phoff, SEEK_SET) < 0)
        goto done;
    programs = (struct elf_phdr *)malloc((size_t)program_bytes);
    if (!programs ||
        !loader_read_exact(descriptor, programs, (size_t)program_bytes))
        goto done;

    if (!main_image.program_headers) {
        size_t path_length = strlen(path);
        if (path_length >= sizeof(main_image.path)) goto done;
        struct elf_phdr *header_copy =
            (struct elf_phdr *)malloc((size_t)program_bytes);
        if (!header_copy) goto done;
        memcpy(header_copy, programs, (size_t)program_bytes);

        uintptr_t load_bias = 0U;
        int found_image_bias = header.type == ET_EXEC;
        for (elf_half_t index = 0; index < header.phnum; ++index) {
            const struct elf_phdr *program = &programs[index];
            if (program->type == PT_PHDR) {
                if (program_header_address < (uintptr_t)program->vaddr) {
                    free(header_copy);
                    goto done;
                }
                load_bias = program_header_address -
                            (uintptr_t)program->vaddr;
                found_image_bias = 1;
                break;
            }
            if (program->type != PT_LOAD ||
                (uint64_t)header.phoff < (uint64_t)program->offset ||
                (uint64_t)header.phoff - (uint64_t)program->offset >
                    (uint64_t)program->filesz ||
                program_bytes > (uint64_t)program->filesz -
                    ((uint64_t)header.phoff - (uint64_t)program->offset))
                continue;
            uint64_t phdr_vaddr = (uint64_t)program->vaddr +
                ((uint64_t)header.phoff - (uint64_t)program->offset);
            if (program_header_address < phdr_vaddr) {
                free(header_copy);
                goto done;
            }
            load_bias = program_header_address - (uintptr_t)phdr_vaddr;
            found_image_bias = 1;
            break;
        }
        /* XSCore's static-PIE linker scripts intentionally keep the ELF
         * header/program-header table outside PT_LOAD. The kernel publishes
         * AT_PHDR as load_base + e_phoff in that case, so recover the same
         * load bias from the validated auxiliary-vector address. */
        if (!found_image_bias &&
            (uint64_t)(uintptr_t)header.phoff == (uint64_t)header.phoff &&
            program_header_address >= (uintptr_t)header.phoff) {
            load_bias = program_header_address - (uintptr_t)header.phoff;
            found_image_bias = 1;
        }
        if (!found_image_bias) {
            free(header_copy);
            goto done;
        }
        main_image.program_headers = header_copy;
        main_image.program_header_count = header.phnum;
        main_image.load_bias = load_bias;
        memcpy(main_image.path, path, path_length + 1U);
        ++loader_object_add_count;
    }

    uint64_t tls_offset = 0;
    uint64_t tls_filesz = 0;
    uint64_t tls_memsz = 0;
    uint64_t tls_align = sizeof(uintptr_t);
    int tls_present = 0;
    for (elf_half_t index = 0; index < header.phnum; ++index) {
        const struct elf_phdr *program = &programs[index];
        if (program->type != PT_TLS) continue;
        if (tls_present || program->memsz < program->filesz ||
            program->memsz > AKAOS_TLS_IMAGE_MAX ||
            !range_ok((size_t)status.size, (uint64_t)program->offset,
                      (uint64_t)program->filesz))
            goto done;
        tls_present = 1;
        tls_offset = (uint64_t)program->offset;
        tls_filesz = (uint64_t)program->filesz;
        tls_memsz = (uint64_t)program->memsz;
        tls_align = (uint64_t)program->align;
        if (tls_align == 0U) tls_align = 1U;
        if ((tls_align & (tls_align - 1U)) != 0U ||
            tls_align > 4096U)
            goto done;
    }
    if (!tls_present || tls_memsz == 0U) {
        success = 1;
        goto done;
    }
    if (!ensure_object_table()) goto done;

    size_t aligned_tls_size =
        static_tls_align_up((size_t)tls_memsz, (size_t)tls_align);
    if (aligned_tls_size == SIZE_MAX ||
        aligned_tls_size > AKAOS_STATIC_TLS_DATA_LIMIT -
                               AKAOS_STATIC_TLS_DATA_START ||
        objects[0].used)
        goto done;
    for (int index = 1; index < DL_OBJECT_MAX; ++index)
        if (objects[index].used) goto done;

    tls_template = (unsigned char *)malloc((size_t)tls_memsz);
    if (!tls_template) goto done;
    memset(tls_template, 0, (size_t)tls_memsz);
    if (tls_filesz != 0U &&
        ((uint64_t)(off_t)tls_offset != tls_offset ||
         lseek(descriptor, (off_t)tls_offset, SEEK_SET) < 0 ||
         !loader_read_exact(descriptor, tls_template, (size_t)tls_filesz)))
        goto done;

    size_t tls_start = 0U;
    intptr_t tls_tp_offset = 0;
#if AKAOS_STATIC_TLS_POSITIVE
    tls_start = static_tls_align_thread_offset(AKAOS_STATIC_TLS_DATA_START,
                                               (size_t)tls_align);
    if (tls_start == SIZE_MAX ||
        tls_start > AKAOS_STATIC_TLS_DATA_LIMIT - aligned_tls_size)
        goto done;
    tls_tp_offset = (intptr_t)tls_start;
#else
    size_t alignment_mask = (size_t)tls_align - 1U;
    size_t thread_pointer =
        static_tls_tp_offset_value & ~alignment_mask;
    if (thread_pointer < aligned_tls_size) goto done;
    tls_start = thread_pointer - aligned_tls_size;
    tls_tp_offset = -(intptr_t)aligned_tls_size;
    static_tls_tp_offset_value = thread_pointer;
#endif

    struct dl_object *main_object = &objects[0];
    memset(main_object, 0, sizeof(*main_object));
    main_object->used = 1;
    main_object->references = UINT32_MAX;
    main_object->tls_module = 1U;
    main_object->tls_template = tls_template;
    tls_template = NULL;
    main_object->tls_filesz = (size_t)tls_filesz;
    main_object->tls_size = (size_t)tls_memsz;
    main_object->tls_align = (size_t)tls_align;
    main_object->static_tls_start = tls_start;
    main_object->static_tls_offset = tls_tp_offset;
    main_object->static_tls_assigned = 1;
    ++static_tls_generation;
    if (static_tls_generation == 0U) static_tls_generation = 1U;
    main_object->static_tls_generation = static_tls_generation;
    ++static_tls_object_count;
    if (!akaos_dl_static_tls_prepare()) goto done;
    success = 1;

done:
    if (descriptor >= 0) close(descriptor);
    free(programs);
    free(tls_template);
    errno = saved_errno;
    return success;
}

static void *dlopen_locked(const char *path, int mode) {
    clear_error();
    if (!path) return &main_module_handle;
    if (path[0] == '\0') {
        set_error("dlopen: invalid path");
        errno = EINVAL;
        return NULL;
    }
    char resolved_path[AKAOS_DSO_PATH_MAX];
    path = resolve_dlopen_library_path(path, resolved_path,
                                       sizeof(resolved_path));
    if (!ensure_object_table()) return NULL;
    path = canonical_library_path(path);
    loader_trace_path("begin", path);
    for (int existing = 0; existing < DL_OBJECT_MAX; ++existing) {
        if (!objects[existing].used || objects[existing].loading ||
            !objects[existing].path || strcmp(objects[existing].path, path) != 0) continue;
        if (mode & RTLD_GLOBAL) objects[existing].global_scope = 1;
        ++objects[existing].references;
        return &objects[existing];
    }
    if (mode & RTLD_NOLOAD) {
        set_error("dlopen: object is not already loaded");
        errno = ENOENT;
        return NULL;
    }
    int slot = -1;
    for (int i = 0; i < DL_OBJECT_MAX; ++i) if (!objects[i].used) { slot = i; break; }
    if (slot < 0) { set_error("dlopen: object limit reached"); errno = ENOMEM; return NULL; }
    struct dl_object *object = &objects[slot];
    reset_object(object);
    object->used = 1;
    object->loading = 1;
    object->global_scope = (mode & RTLD_GLOBAL) != 0;
    object->references = 1;
    size_t path_length = strlen(path);
    if (path_length >= sizeof(object->path_storage)) {
        reset_object(object); set_error("dlopen: path too long"); errno = ENAMETOOLONG; return NULL;
    }
    memcpy(object->path_storage, path, path_length + 1);
    object->path = object->path_storage;
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        loader_trace_path("open-failed", path);
        reset_object(object);
        char message[DL_ERROR_MAX];
        (void)snprintf(message, sizeof(message),
                       "dlopen: cannot open object: %s", path);
        set_error(message);
        return NULL;
    }
    loader_trace_path("opened", path);
    struct akaos_stat status;
    if (stat(path, &status) < 0 || status.size < sizeof(struct elf_ehdr) ||
        status.size > AKAOS_DSO_IMAGE_MAX) {
        close(descriptor); reset_object(object); set_error("dlopen: invalid object size"); errno = EINVAL; return NULL;
    }
    size_t file_size = (size_t)status.size;
    /* Map small immutable DSOs directly. Large toolkit libraries currently
     * exercise a slow VFS file-mmap path on akaOS, so read those in bounded
     * chunks instead. Keep the read fallback for filesystems that cannot map
     * even small files. */
    int used_read_fallback = file_size > 8U * 1024U * 1024U;
    int map_error = 0;
    unsigned char *image = MAP_FAILED;
    if (!used_read_fallback) {
        image = (unsigned char *)mmap(
            NULL, file_size, PROT_READ, MAP_PRIVATE, descriptor, 0);
        if (image == MAP_FAILED) map_error = errno;
    }
    if (image == MAP_FAILED) {
        used_read_fallback = 1;
        if (map_error == 0) map_error = errno;
        image = (unsigned char *)mmap(
            NULL, file_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (image == MAP_FAILED) {
            char message[DL_ERROR_MAX];
            (void)snprintf(message, sizeof(message),
                           "dlopen: file-image mapping failed (%d): %s",
                           map_error, path);
            close(descriptor);
            reset_object(object);
            set_error(message);
            errno = ENOMEM;
            return NULL;
        }
        size_t received = 0;
        while (received < file_size) {
            /* Match the kernel's bounded I/O transfer size, while keeping
             * the loop correct if a file read still returns a short result. */
            size_t chunk = file_size - received;
            if (chunk > AKAOS_SYSCALL_MAX_IO)
                chunk = AKAOS_SYSCALL_MAX_IO;
            ssize_t count = read(descriptor, image + received, chunk);
            if (count <= 0) {
                release_loader_image(image, file_size);
                close(descriptor);
                reset_object(object);
                set_error("dlopen: read failed");
                return NULL;
            }
            received += (size_t)count;
        }
    }
    loader_trace_path(used_read_fallback ? "read-fallback" : "file-mapped",
                      path);
    close(descriptor);
    const struct elf_ehdr *header = (const struct elf_ehdr *)image;
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' || header->ident[2] != 'L' ||
        header->ident[3] != 'F' || header->ident[4] != ELF_CLASS || header->ident[5] != ELFDATA2LSB) {
        release_loader_image(image, file_size); reset_object(object); set_error("dlopen: invalid ELF identification"); errno = EINVAL; return NULL;
    }
    if (header->type != ET_DYN) {
        release_loader_image(image, file_size); reset_object(object); set_error("dlopen: object is not ET_DYN"); errno = EINVAL; return NULL;
    }
    if (!machine_matches(header->machine)) {
        release_loader_image(image, file_size); reset_object(object); set_error("dlopen: object architecture mismatch"); errno = EINVAL; return NULL;
    }
    if (header->phentsize != sizeof(struct elf_phdr) || header->phnum == 0 ||
        !range_ok(file_size, (uint64_t)header->phoff,
                  (uint64_t)header->phnum * sizeof(struct elf_phdr))) {
        release_loader_image(image, file_size); reset_object(object); set_error("dlopen: malformed program headers"); errno = EINVAL; return NULL;
    }
    loader_trace_path("elf-header", path);
    const struct elf_phdr *programs = (const struct elf_phdr *)(image + header->phoff);
    size_t program_header_bytes =
        (size_t)header->phnum * sizeof(struct elf_phdr);
    object->program_headers =
        (struct elf_phdr *)malloc(program_header_bytes);
    if (!object->program_headers) {
        release_loader_image(image, file_size);
        reset_object(object);
        set_error("dlopen: program-header allocation failed");
        errno = ENOMEM;
        return NULL;
    }
    loader_trace_path("program-headers-allocated", path);
    memcpy(object->program_headers, programs, program_header_bytes);
    object->program_header_count = header->phnum;
    uint64_t minimum = UINT64_MAX, maximum = 0, dynamic_vaddr = 0, dynamic_size = 0;
    uint64_t tls_offset = 0, tls_filesz = 0, tls_memsz = 0, tls_align = 0;
    int tls_present = 0;
    for (elf_half_t i = 0; i < header->phnum; ++i) {
        const struct elf_phdr *program = &programs[i];
        if (program->type == PT_LOAD) {
            uint64_t start = (uint64_t)program->vaddr & ~UINT64_C(0xfff);
            uint64_t end = ((uint64_t)program->vaddr + (uint64_t)program->memsz + 0xfff) & ~UINT64_C(0xfff);
            if (program->memsz < program->filesz || !range_ok(file_size, (uint64_t)program->offset, (uint64_t)program->filesz) || end < start) {
                release_loader_image(image, file_size); reset_object(object); set_error("dlopen: malformed load segment"); errno = EINVAL; return NULL;
            }
            if (start < minimum) minimum = start;
            if (end > maximum) maximum = end;
        } else if (program->type == PT_DYNAMIC) {
            dynamic_vaddr = (uint64_t)program->vaddr;
            dynamic_size = (uint64_t)program->filesz;
        } else if (program->type == PT_TLS) {
            if (program->memsz < program->filesz ||
                !range_ok(file_size, (uint64_t)program->offset,
                          (uint64_t)program->filesz) ||
                program->memsz > AKAOS_TLS_IMAGE_MAX) {
                release_loader_image(image, file_size); reset_object(object);
                set_error("dlopen: malformed TLS segment"); errno = EINVAL; return NULL;
            }
            tls_present = 1;
            tls_offset = (uint64_t)program->offset;
            tls_filesz = (uint64_t)program->filesz;
            tls_memsz = (uint64_t)program->memsz;
            tls_align = (uint64_t)program->align;
        }
    }
    loader_trace_path("program-headers-scanned", path);
    if (minimum == UINT64_MAX || maximum <= minimum || maximum - minimum > SIZE_MAX) {
        release_loader_image(image, file_size); reset_object(object); set_error("dlopen: no loadable segments"); errno = EINVAL; return NULL;
    }
    size_t mapping_length = (size_t)(maximum - minimum);
    void *mapping = mmap(NULL, mapping_length, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) { release_loader_image(image, file_size); reset_object(object); set_error("dlopen: address space exhausted"); return NULL; }
    loader_trace_path("segment-map-allocated", path);
    object->mapping = mapping; object->mapping_length = mapping_length;
    if (loader_trace_enabled()) {
        printf("loader: mapped %s at %p length %zu\n", path, mapping,
               mapping_length);
        fflush(stdout);
    }
    object->min_vaddr = minimum; object->max_vaddr = maximum;
    object->load_bias = (uintptr_t)mapping - (uintptr_t)minimum;
    object->entry = object->load_bias + (uintptr_t)header->entry;
    if (tls_present && tls_memsz != 0) {
        object->tls_template = (unsigned char *)malloc((size_t)tls_memsz);
        if (!object->tls_template) {
            munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
            set_error("dlopen: TLS template allocation failed"); errno = ENOMEM; return NULL;
        }
        memset(object->tls_template, 0, (size_t)tls_memsz);
        if (tls_filesz != 0) {
            memcpy(object->tls_template, image + tls_offset, (size_t)tls_filesz);
        }
        object->tls_module = (uintptr_t)(slot + 1);
        object->tls_filesz = (size_t)tls_filesz;
        object->tls_size = (size_t)tls_memsz;
        object->tls_align = tls_align > SIZE_MAX ? sizeof(uintptr_t) :
                             (size_t)tls_align;
        object->tls_mapping_length = ((size_t)tls_memsz + 4095U) &
                                     ~((size_t)4095U);
        if (object->tls_mapping_length == 0) object->tls_mapping_length = 4096U;
        if (!static_tls_assign_object(object)) {
            munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
            return NULL;
        }
    }
    for (elf_half_t i = 0; i < header->phnum; ++i) {
        const struct elf_phdr *program = &programs[i];
        if (program->type != PT_LOAD) continue;
        uint64_t destination = (uint64_t)program->vaddr - minimum;
        memcpy((unsigned char *)mapping + destination, image + program->offset, program->filesz);
        if (program->memsz > program->filesz) memset((unsigned char *)mapping + destination + program->filesz, 0, program->memsz - program->filesz);
    }
    loader_trace_path("segments-copied", path);
    /* Every PT_LOAD byte and the TLS template have been copied.  Drop the
     * temporary file image before recursively opening DT_NEEDED objects;
     * retaining each parent's file copy multiplies peak mmap usage for large
     * libraries such as GIO. */
    release_loader_image(image, file_size);
    image = NULL;
    char mapped_path[AKAOS_DSO_PATH_MAX + 32U];
    (void)snprintf(mapped_path, sizeof(mapped_path), "%s @0x%llx", path,
                   (unsigned long long)object->load_bias);
    loader_trace_path("segments", mapped_path);
    if (dynamic_vaddr == 0 || dynamic_vaddr < minimum || dynamic_size < sizeof(struct elf_dyn)) {
        munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object); set_error("dlopen: object has no dynamic section"); errno = EINVAL; return NULL;
    }
    const struct elf_dyn *dynamic = (const struct elf_dyn *)mapped_address(object, dynamic_vaddr);
    size_t dynamic_count;
#if AKAOS_ELF64
    dynamic_count = (size_t)(dynamic_size / sizeof(struct elf_dyn));
#else
    dynamic_count = (size_t)((uint32_t)dynamic_size / (uint32_t)sizeof(struct elf_dyn));
#endif
    if (!mapping_range_ok(object, dynamic_vaddr, dynamic_size)) {
        munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
        set_error("dlopen: dynamic section outside image"); errno = EINVAL; return NULL;
    }
    uint64_t strtab_vaddr = 0, symtab_vaddr = 0, strtab_size = 0,
             syment = sizeof(struct elf_sym), hash_vaddr = 0, gnu_hash_vaddr = 0;
    uint64_t init_vaddr = 0, fini_vaddr = 0, init_array_vaddr = 0,
             fini_array_vaddr = 0, init_array_size = 0, fini_array_size = 0;
    uint64_t versym_vaddr = 0, verdef_vaddr = 0, verneed_vaddr = 0;
    uint64_t verdef_count = 0, verneed_count = 0;
    uint64_t needed_offsets[AKAOS_DL_DEPENDENCY_MAX];
    uint32_t needed_count = 0;
    for (size_t i = 0; i < dynamic_count; ++i) {
        int64_t tag = (int64_t)dynamic[i].tag;
        uint64_t value = (uint64_t)dynamic[i].value;
        if (tag == DT_NULL) break;
        if (tag == DT_NEEDED) {
            if (needed_count >= AKAOS_DL_DEPENDENCY_MAX) {
                munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
                set_error("dlopen: too many dependencies"); errno = ENOMEM; return NULL;
            }
            needed_offsets[needed_count++] = value;
        }
        if (tag == DT_STRTAB) strtab_vaddr = value;
        else if (tag == DT_SYMTAB) symtab_vaddr = value;
        else if (tag == DT_STRSZ) strtab_size = value;
        else if (tag == DT_SYMENT) syment = value;
        else if (tag == DT_HASH) hash_vaddr = value;
        else if (tag == DT_GNU_HASH) gnu_hash_vaddr = value;
        else if (tag == DT_INIT) init_vaddr = value;
        else if (tag == DT_FINI) fini_vaddr = value;
        else if (tag == DT_INIT_ARRAY) init_array_vaddr = value;
        else if (tag == DT_FINI_ARRAY) fini_array_vaddr = value;
        else if (tag == DT_INIT_ARRAYSZ) init_array_size = value;
        else if (tag == DT_FINI_ARRAYSZ) fini_array_size = value;
        else if (tag == DT_VERSYM) versym_vaddr = value;
        else if (tag == DT_VERDEF) verdef_vaddr = value;
        else if (tag == DT_VERDEFNUM) verdef_count = value;
        else if (tag == DT_VERNEED) verneed_vaddr = value;
        else if (tag == DT_VERNEEDNUM) verneed_count = value;
    }
    loader_trace_path("dynamic-tags", path);
    if (!mapped_callback(object, init_vaddr, &object->init) ||
        !mapped_callback(object, fini_vaddr, &object->fini) ||
        init_array_size > (uint64_t)SIZE_MAX ||
        fini_array_size > (uint64_t)SIZE_MAX ||
        init_array_size % sizeof(uintptr_t) != 0 ||
        fini_array_size % sizeof(uintptr_t) != 0 ||
        init_array_size / sizeof(uintptr_t) > AKAOS_INIT_ARRAY_MAX ||
        fini_array_size / sizeof(uintptr_t) > AKAOS_INIT_ARRAY_MAX ||
        (init_array_size != 0 &&
         (!init_array_vaddr || !mapping_range_ok(object, init_array_vaddr,
                                                  init_array_size))) ||
        (fini_array_size != 0 &&
         (!fini_array_vaddr || !mapping_range_ok(object, fini_array_vaddr,
                                                  fini_array_size)))) {
        munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
        set_error("dlopen: malformed constructor table"); errno = EINVAL; return NULL;
    }
    if (init_array_size != 0) {
        object->init_array = mapped_address(object, init_array_vaddr);
        object->init_array_count = (size_t)(init_array_size / sizeof(uintptr_t));
    }
    if (fini_array_size != 0) {
        object->fini_array = mapped_address(object, fini_array_vaddr);
        object->fini_array_count = (size_t)(fini_array_size / sizeof(uintptr_t));
    }
    object->strtab = mapping_range_ok(object, strtab_vaddr, strtab_size) ?
                     (const char *)mapped_address(object, strtab_vaddr) : NULL;
    object->strtab_size = (size_t)strtab_size;
    object->symtab = mapping_range_ok(object, symtab_vaddr,
                                      sizeof(struct elf_sym)) ?
                     (const struct elf_sym *)mapped_address(object, symtab_vaddr) : NULL;
    if (!object->strtab || !object->symtab || syment != sizeof(struct elf_sym) || object->strtab_size == 0) {
        munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object); set_error("dlopen: missing dynamic symbol metadata"); errno = EINVAL; return NULL;
    }
    if (hash_vaddr && mapping_range_ok(object, hash_vaddr, 2 * sizeof(uint32_t))) {
        const uint32_t *hash = (const uint32_t *)mapped_address(object, hash_vaddr);
        object->symbol_count = hash[1];
    }
    if (object->symbol_count == 0 && gnu_hash_vaddr) {
        (void)gnu_hash_symbol_count(object, gnu_hash_vaddr, &object->symbol_count);
    }
    if (object->symbol_count == 0) {
        munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object); set_error("dlopen: missing symbol hash"); errno = EINVAL; return NULL;
    }
    if (verdef_count > AKAOS_VERSION_MAX || verneed_count > AKAOS_VERSION_MAX ||
        (versym_vaddr != 0 &&
         ((uint64_t)object->symbol_count > UINT64_MAX / sizeof(uint16_t) ||
          !mapping_range_ok(object, versym_vaddr,
                            (uint64_t)object->symbol_count * sizeof(uint16_t)))) ||
        (verdef_count != 0 &&
         (verdef_vaddr == 0 ||
          !mapping_range_ok(object, verdef_vaddr, sizeof(struct elf_verdef)))) ||
        (verneed_count != 0 &&
         (verneed_vaddr == 0 ||
          !mapping_range_ok(object, verneed_vaddr, sizeof(struct elf_verneed))) ) ||
        ((verdef_vaddr != 0 || verneed_vaddr != 0) && versym_vaddr == 0)) {
        munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
        set_error("dlopen: malformed symbol-version metadata");
        errno = EINVAL;
        return NULL;
    }
    if (versym_vaddr != 0)
        object->versym = (const uint16_t *)mapped_address(object, versym_vaddr);
    object->gnu_hash_vaddr = gnu_hash_vaddr;
    object->verdef_vaddr = verdef_vaddr;
    object->verdef_count = (uint32_t)verdef_count;
    object->verneed_vaddr = verneed_vaddr;
    object->verneed_count = (uint32_t)verneed_count;
    loader_trace_path("symbols", path);
    for (uint32_t dependency = 0; dependency < needed_count; ++dependency) {
        uint64_t name_offset = needed_offsets[dependency];
        if (name_offset >= object->strtab_size) {
            close_dependencies(object); munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
            set_error("dlopen: dependency name outside string table"); errno = EINVAL; return NULL;
        }
        const char *name = object->strtab + name_offset;
        size_t remaining = object->strtab_size - (size_t)name_offset;
        size_t name_length = 0;
        while (name_length < remaining && name[name_length]) ++name_length;
        if (name_length == remaining || name_length == 0) {
            close_dependencies(object); munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
            set_error("dlopen: malformed dependency name"); errno = EINVAL; return NULL;
        }
        char dependency_path[AKAOS_DSO_PATH_MAX];
        const char *dependency_prefix = "/lib/";
        if (name[0] != '/' && upstream_qtbase_enabled() &&
            upstream_library_exists(name, name_length))
            dependency_prefix = "/usr/local/lib/";
        size_t prefix_length = name[0] == '/' ? 0 : strlen(dependency_prefix);
        if (prefix_length + name_length >= sizeof(dependency_path)) {
            close_dependencies(object); munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
            set_error("dlopen: dependency path too long"); errno = ENAMETOOLONG; return NULL;
        }
        if (prefix_length)
            memcpy(dependency_path, dependency_prefix, prefix_length);
        memcpy(dependency_path + prefix_length, name, name_length + 1);
        loader_trace_path("needed", dependency_path);
        void *dependency_handle = dlopen(dependency_path, RTLD_NOW | RTLD_GLOBAL);
        if (!dependency_handle) {
            char dependency_error[DL_ERROR_MAX] = {0};
            char *detail = dlerror();
            if (detail) {
                size_t detail_length = strlen(detail);
                if (detail_length >= sizeof(dependency_error))
                    detail_length = sizeof(dependency_error) - 1U;
                memcpy(dependency_error, detail, detail_length);
                dependency_error[detail_length] = '\0';
            }
            char dependency_message[DL_ERROR_MAX];
            if (dependency_error[0]) {
                memcpy(dependency_message, dependency_error,
                       sizeof(dependency_message));
            } else {
                (void)snprintf(dependency_message, sizeof(dependency_message),
                               "dlopen: dependency %s failed",
                               dependency_path);
            }
            close_dependencies(object); munmap(mapping, mapping_length); release_loader_image(image, file_size); reset_object(object);
            set_error(dependency_message);
            errno = ENOENT; return NULL;
        }
        object->dependencies[object->dependency_count++] =
            (struct dl_object *)dependency_handle;
    }
    loader_trace_path("dependencies", path);
    if (object->static_tls_assigned && !akaos_dl_static_tls_prepare()) {
        int tls_errno = errno;
        char tls_error[DL_ERROR_MAX] = {0};
        char *detail = dlerror();
        if (detail) {
            size_t detail_length = strlen(detail);
            if (detail_length >= sizeof(tls_error))
                detail_length = sizeof(tls_error) - 1U;
            memcpy(tls_error, detail, detail_length);
            tls_error[detail_length] = '\0';
        }
        close_dependencies(object); munmap(mapping, mapping_length); release_loader_image(image, file_size);
        reset_object(object);
        set_error(tls_error[0] ? tls_error : "dlopen: static TLS setup failed");
        errno = tls_errno ? tls_errno : ENOMEM;
        return NULL;
    }
    if (!apply_relocations(object, dynamic, dynamic_count)) {
        int relocation_errno = errno;
        char relocation_error[DL_ERROR_MAX] = {0};
        char *detail = dlerror();
        if (detail) {
            size_t detail_length = strlen(detail);
            if (detail_length >= sizeof(relocation_error))
                detail_length = sizeof(relocation_error) - 1U;
            memcpy(relocation_error, detail, detail_length);
            relocation_error[detail_length] = '\0';
        }
        close_dependencies(object);
        munmap(mapping, mapping_length);
        release_loader_image(image, file_size);
        reset_object(object);
        set_error(relocation_error[0] ? relocation_error :
                  "dlopen: relocation failed");
        errno = relocation_errno;
        return NULL;
    }
    loader_trace_path("relocated", path);
    object->loading = 0;
    ++loader_object_add_count;
    uintptr_t namespace_binder_address = 0;
    if (symbol_value_by_name(object, "akaos_dl_bind_namespace", NULL,
                             &namespace_binder_address)) {
        size_t thread_pointer_offset = 0U;
#if !AKAOS_STATIC_TLS_POSITIVE
        thread_pointer_offset = static_tls_tp_offset_value;
#endif
        typedef int (*namespace_binder_fn)(void *, uint64_t, uint64_t, void *,
                                           uint64_t, uint32_t, size_t);
        namespace_binder_fn bind_namespace =
            (namespace_binder_fn)namespace_binder_address;
        if (bind_namespace(objects, loader_object_add_count,
                           loader_object_sub_count, static_tls_threads,
                           static_tls_generation, static_tls_object_count,
                           thread_pointer_offset) != 0) {
            close_dependencies(object);
            munmap(mapping, mapping_length);
            release_loader_image(image, file_size);
            reset_object(object);
            set_error("dlopen: shared loader namespace binding failed");
            errno = EINVAL;
            return NULL;
        }
    }
    loader_trace_path("initializers", path);
    run_object_initializers(object);
    loader_trace_path("ready", path);
    release_loader_image(image, file_size);
        return object;
}

/* A shared libc loaded by the initial ELF interpreter must use the same
 * object namespace as that interpreter.  The process is single-address-space;
 * sharing this stable table pointer keeps dl_iterate_phdr and subsequent
 * dlopen/dlclose calls aware of the executable and its already-loaded DSOs. */
__attribute__((visibility("default")))
int akaos_dl_bind_namespace(void *namespace_pointer, uint64_t adds,
                            uint64_t subs, void *tls_thread_table,
                            uint64_t tls_generation, uint32_t tls_object_count,
                            size_t thread_pointer_offset) {
    if (!namespace_pointer) return EINVAL;
    if (!tls_thread_table) return EINVAL;
    if (objects && objects != namespace_pointer) return EBUSY;
    objects = (struct dl_object *)namespace_pointer;
    loader_object_add_count = adds;
    loader_object_sub_count = subs;
    static_tls_threads =
        (struct akaos_static_tls_thread *)tls_thread_table;
    static_tls_generation = tls_generation;
    static_tls_object_count = tls_object_count;
#if !AKAOS_STATIC_TLS_POSITIVE
    static_tls_tp_offset_value = thread_pointer_offset;
#else
    (void)thread_pointer_offset;
#endif
    return 0;
}

static void *object_tls_data_for_current_thread(struct dl_object *object) {
    if (!object || !object->tls_module || object->tls_size == 0) return NULL;
    pthread_t thread = pthread_self();
    if (!thread) return NULL;
    if (object->static_tls_assigned) {
        int index = static_tls_find_thread(thread);
        if (index < 0) return NULL;
        return static_tls_object_address(&static_tls_threads[index], object, 0);
    }
    for (size_t index = 0; index < AKAOS_TLS_THREAD_SLOTS; ++index) {
        if (object->tls_thread_ids[index] == (uint32_t)thread)
            return object->tls_blocks[index];
    }
    return NULL;
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
                    void *data) {
    if (!callback) {
        errno = EINVAL;
        return -1;
    }
    if (!loader_lock()) {
        errno = EAGAIN;
        return -1;
    }
    if (!ensure_object_table()) {
        loader_unlock();
        return -1;
    }

    int result = 0;
    if (main_image.program_headers && main_image.program_header_count != 0U) {
        struct dl_phdr_info info;
        memset(&info, 0, sizeof(info));
        info.dlpi_addr = (Elf_Addr)main_image.load_bias;
        info.dlpi_name = main_image.path;
        info.dlpi_phdr = (const Elf_Phdr *)main_image.program_headers;
        info.dlpi_phnum = (Elf_Half)main_image.program_header_count;
        info.dlpi_adds = loader_object_add_count;
        info.dlpi_subs = loader_object_sub_count;
        if (objects[0].used) {
            info.dlpi_tls_modid = objects[0].tls_module;
            info.dlpi_tls_data = object_tls_data_for_current_thread(&objects[0]);
        }
        result = callback(&info, sizeof(info), data);
    }
    for (int index = 0; index < DL_OBJECT_MAX; ++index) {
        if (result != 0) break;
        struct dl_object *object = &objects[index];
        if (!object->used || object->loading || !object->program_headers ||
            object->program_header_count == 0)
            continue;
        struct dl_phdr_info info;
        memset(&info, 0, sizeof(info));
        info.dlpi_addr = (Elf_Addr)object->load_bias;
        /* The ELF e_entry is normally zero for a DSO, but load_bias is
         * nonzero after mapping.  Keep the canonical path for every image;
         * unlike glibc's empty main-program name, this is unambiguous for
         * loader diagnostics and still conforms to dl_iterate_phdr callers. */
        info.dlpi_name = object->path;
        info.dlpi_phdr = (const Elf_Phdr *)object->program_headers;
        info.dlpi_phnum = (Elf_Half)object->program_header_count;
        info.dlpi_adds = loader_object_add_count;
        info.dlpi_subs = loader_object_sub_count;
        info.dlpi_tls_modid = object->tls_module;
        info.dlpi_tls_data = object_tls_data_for_current_thread(object);
        result = callback(&info, sizeof(info), data);
        if (result != 0) break;
    }
    loader_unlock();
    return result;
}

void *dlopen(const char *path, int mode) {
    if (!loader_lock()) {
        set_error("dlopen: recursive loader mutex unavailable");
        errno = EAGAIN;
        return NULL;
    }
    void *object = dlopen_locked(path, mode);
    loader_unlock();
    return object;
}

/* akaOS's first native ELF interpreter uses the same bounded ET_DYN loader as
 * applications.  Keep this small accessor private to the akaOS ABI instead of
 * pretending that a POSIX dlsym handle is itself an executable entry point. */
static void *dl_entry_locked(void *handle) {
    if (!handle || !objects) {
        set_error("akaos_dl_entry: invalid handle");
        errno = EINVAL;
        return NULL;
    }
    struct dl_object *object = (struct dl_object *)handle;
    for (int i = 0; i < DL_OBJECT_MAX; ++i) {
        if (object == &objects[i] && object->used && object->entry != 0) {
            return (void *)object->entry;
        }
    }
    set_error("akaos_dl_entry: invalid handle");
    errno = EINVAL;
    return NULL;
}

void *akaos_dl_entry(void *handle) {
    if (!loader_lock()) {
        set_error("akaos_dl_entry: loader mutex unavailable");
        errno = EAGAIN;
        return NULL;
    }
    void *entry = dl_entry_locked(handle);
    loader_unlock();
    return entry;
}

static int lookup_handle_symbol_recursive(struct dl_object *object,
                                          const char *name,
                                          const char *version,
                                          uintptr_t *value,
                                          uint64_t visited) {
    if (!object || !name || !value || !objects) return 0;
    int index = -1;
    for (int slot = 0; slot < DL_OBJECT_MAX; ++slot) {
        if (&objects[slot] == object) {
            index = slot;
            break;
        }
    }
    if (index < 0 || (visited & (UINT64_C(1) << index))) return 0;
    visited |= UINT64_C(1) << index;
    if (symbol_value_by_name(object, name, version, value)) return 1;
    for (uint32_t dependency = 0; dependency < object->dependency_count;
         ++dependency) {
        if (lookup_handle_symbol_recursive(object->dependencies[dependency],
                                           name, version, value, visited))
            return 1;
    }
    return 0;
}

static int lookup_handle_symbol(struct dl_object *object, const char *name,
                                const char *version, uintptr_t *value) {
    return lookup_handle_symbol_recursive(object, name, version, value, 0);
}

static struct dl_object *validated_handle(void *handle) {
    if (!handle || !objects) return NULL;
    struct dl_object *object = (struct dl_object *)handle;
    for (int i = 0; i < DL_OBJECT_MAX; ++i)
        if (object == &objects[i] && object->used) return object;
    return NULL;
}

static int object_index_for_address(uintptr_t address) {
    if (address == 0 || !objects) return -1;
    for (int slot = 0; slot < DL_OBJECT_MAX; ++slot) {
        const struct dl_object *object = &objects[slot];
        if (!object->used || !object->mapping || object->mapping_length == 0)
            continue;
        uintptr_t begin = (uintptr_t)object->mapping;
        if (address >= begin && address - begin < object->mapping_length)
            return slot;
    }
    return -1;
}

int dladdr(const void *address, Dl_info *information) {
    if (!address || !information) {
        errno = EINVAL;
        return 0;
    }
    memset(information, 0, sizeof(*information));
    if (!loader_lock()) {
        errno = EAGAIN;
        return 0;
    }
    if (!ensure_object_table()) {
        loader_unlock();
        return 0;
    }

    uintptr_t target = (uintptr_t)address;
    int found = 0;
    int object_index = object_index_for_address(target);
    if (object_index >= 0) {
        struct dl_object *object = &objects[object_index];
        information->dli_fname = object->path;
        information->dli_fbase = (void *)object->load_bias;
        uintptr_t best_address = 0U;
        for (size_t index = 0; index < object->symbol_count; ++index) {
            const struct elf_sym *symbol = &object->symtab[index];
            unsigned int symbol_type = symbol->info & 0x0fU;
            if (symbol->shndx == SHN_UNDEF ||
                (symbol_type != 0U && symbol_type != 2U) ||
                symbol->name >= object->strtab_size)
                continue;
            const char *name = object->strtab + symbol->name;
            size_t remaining = object->strtab_size - symbol->name;
            size_t name_length = 0U;
            while (name_length < remaining && name[name_length]) ++name_length;
            if (name_length == 0U || name_length == remaining) continue;
            uintptr_t symbol_address =
                object->load_bias + (uintptr_t)symbol->value;
            if (symbol_address > target || symbol_address < best_address)
                continue;
            best_address = symbol_address;
            information->dli_sname = name;
            information->dli_saddr = (void *)symbol_address;
        }
        found = 1;
    } else if (main_image.program_headers) {
        for (elf_half_t index = 0; index < main_image.program_header_count;
             ++index) {
            const struct elf_phdr *program =
                &main_image.program_headers[index];
            if (program->type != PT_LOAD || program->memsz == 0U) continue;
            uintptr_t segment_start = main_image.load_bias +
                                     (uintptr_t)program->vaddr;
            if (target >= segment_start &&
                target - segment_start < (uintptr_t)program->memsz) {
                information->dli_fname = main_image.path;
                information->dli_fbase = (void *)main_image.load_bias;
                found = 1;
                break;
            }
        }
    }

    loader_unlock();
    return found;
}

static int builtin_symbol_value(const char *name, uintptr_t *value) {
    if (!name || !value) return 0;
    /* __tls_get_addr is part of akaOS libc itself.  It is intentionally
     * available from RTLD_DEFAULT/RTLD_NEXT without making every executable
     * publish a private libc symbol table. */
    if (strcmp(name, "__tls_get_addr") == 0 ||
        strcmp(name, "___tls_get_addr") == 0) {
        *value = (uintptr_t)&__tls_get_addr;
        return 1;
    }
    return 0;
}

static int lookup_global_symbol(const char *name, const char *version,
                                int first_slot, uintptr_t *value) {
    if (!name || !value) return 0;
    if (!objects) return builtin_symbol_value(name, value);
    if (first_slot < 0) first_slot = 0;
    if (first_slot >= DL_OBJECT_MAX) first_slot = DL_OBJECT_MAX;
    for (int slot = first_slot; slot < DL_OBJECT_MAX; ++slot) {
        struct dl_object *object = &objects[slot];
        if (!object->used || object->loading || !object->global_scope) continue;
        if (symbol_value_by_name(object, name, version, value)) return 1;
    }
    return builtin_symbol_value(name, value);
}

static void *dlsym_locked(void *handle, const char *name) {
    clear_error();
    if (!name || name[0] == '\0') {
        set_error("dlsym: invalid handle or symbol");
        errno = EINVAL;
        return NULL;
    }
    if (handle == RTLD_DEFAULT || handle == &main_module_handle) {
        uintptr_t value = 0;
        if (lookup_global_symbol(name, NULL, 0, &value)) return (void *)value;
        set_error("dlsym: symbol not found");
        errno = ENOENT;
        return NULL;
    }
    if (handle == RTLD_NEXT) {
        uintptr_t caller = 0;
#if defined(__clang__) || defined(__GNUC__)
        caller = (uintptr_t)__builtin_return_address(0);
#endif
        int owner = object_index_for_address(caller);
        uintptr_t value = 0;
        if (lookup_global_symbol(name, NULL, owner >= 0 ? owner + 1 : 0,
                                 &value)) return (void *)value;
        set_error("dlsym: next symbol not found");
        errno = ENOENT;
        return NULL;
    }
    struct dl_object *object = validated_handle(handle);
    if (!object) { set_error("dlsym: invalid handle"); errno = EINVAL; return NULL; }
    uintptr_t value = 0;
    if (lookup_handle_symbol(object, name, NULL, &value)) return (void *)value;
    set_error("dlsym: symbol not found");
    errno = ENOENT;
    return NULL;
}

void *dlsym(void *handle, const char *name) {
    if (!loader_lock()) {
        set_error("dlsym: recursive loader mutex unavailable");
        errno = EAGAIN;
        return NULL;
    }
    void *value = dlsym_locked(handle, name);
    loader_unlock();
    return value;
}

static void *dlvsym_locked(void *handle, const char *name,
                           const char *version) {
    clear_error();
    if (!name || name[0] == '\0' || !version || version[0] == '\0') {
        set_error("dlvsym: invalid handle, symbol, or version");
        errno = EINVAL;
        return NULL;
    }
    if (handle == RTLD_DEFAULT || handle == &main_module_handle ||
        handle == RTLD_NEXT) {
        int first_slot = 0;
        if (handle == RTLD_NEXT) {
            uintptr_t caller = 0;
#if defined(__clang__) || defined(__GNUC__)
            caller = (uintptr_t)__builtin_return_address(0);
#endif
            int owner = object_index_for_address(caller);
            first_slot = owner >= 0 ? owner + 1 : 0;
        }
        uintptr_t value = 0;
        if (lookup_global_symbol(name, version, first_slot, &value))
            return (void *)value;
        set_error("dlvsym: symbol version not found");
        errno = ENOENT;
        return NULL;
    }
    struct dl_object *object = validated_handle(handle);
    if (!object) { set_error("dlvsym: invalid handle"); errno = EINVAL; return NULL; }
    uintptr_t value = 0;
    if (lookup_handle_symbol(object, name, version, &value)) return (void *)value;
    set_error("dlvsym: symbol version not found");
    errno = ENOENT;
    return NULL;
}

void *dlvsym(void *handle, const char *name, const char *version) {
    if (!loader_lock()) {
        set_error("dlvsym: recursive loader mutex unavailable");
        errno = EAGAIN;
        return NULL;
    }
    void *value = dlvsym_locked(handle, name, version);
    loader_unlock();
    return value;
}

static int dlclose_locked(void *handle) {
    clear_error();
    if (handle == &main_module_handle) return 0;
    if (!handle || !objects) { set_error("dlclose: invalid handle"); errno = EINVAL; return -1; }
    struct dl_object *object = (struct dl_object *)handle;
    for (int i = 0; i < DL_OBJECT_MAX; ++i) {
        if (object != &objects[i] || !object->used) continue;
        if (object->references > 1) {
            --object->references;
            return 0;
        }
        struct dl_object *dependencies[AKAOS_DL_DEPENDENCY_MAX];
        uint32_t dependency_count = object->dependency_count;
        for (uint32_t dependency = 0; dependency < dependency_count; ++dependency) {
            dependencies[dependency] = object->dependencies[dependency];
        }
        run_object_finalizers(object);
        int result = munmap(object->mapping, object->mapping_length);
        ++loader_object_sub_count;
        reset_object(object);
        for (uint32_t dependency = 0; dependency < dependency_count; ++dependency) {
            if (dependencies[dependency]) (void)dlclose(dependencies[dependency]);
        }
        return result;
    }
    set_error("dlclose: invalid handle");
    errno = EINVAL;
    return -1;
}

int dlclose(void *handle) {
    if (!loader_lock()) {
        set_error("dlclose: recursive loader mutex unavailable");
        errno = EAGAIN;
        return -1;
    }
    int result = dlclose_locked(handle);
    loader_unlock();
    return result;
}
