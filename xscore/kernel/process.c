#include <xscore/arch.h>
#include <xscore/file.h>
#include <xscore/heap.h>
#include <xscore/memory.h>
#include <xscore/page_cache.h>
#include <xscore/process.h>
#include <xscore/serial.h>
#include <xscore/smp.h>
#include <xscore/syscall.h>
#include <xscore/thread.h>
#include <xscore/vmm.h>
#include <xscore/vfs.h>
#include <xscore/usercopy.h>
#include <akaos/errno.h>
#include <akaos/mman.h>
#include <akaos/syscall.h>
#include <stddef.h>
#include <stdint.h>
#include <akaos/signal.h>

#define PAGE_SIZE UINT64_C(4096)
#if defined(__i386__) || (defined(__riscv) && __riscv_xlen == 32)
#define USER_CODE_ADDRESS UINT64_C(0x40000000)
#elif defined(__arm__)
#define USER_CODE_ADDRESS UINT64_C(0x10000000)
#elif (defined(__riscv) && __riscv_xlen == 64)
#define USER_CODE_ADDRESS UINT64_C(0x0000002000000000)
#else
#define USER_CODE_ADDRESS UINT64_C(0x0000008000000000)
#endif
#define USER_STACK_TOP (USER_CODE_ADDRESS + UINT64_C(0x00201000))
#define USER_STACK_PAGE (USER_STACK_TOP - PAGE_SIZE)
#define USER_MAX_STACK_PAGES 64U
#define USER_TRACKED_PAGE_CAPACITY \
    (USER_IMAGE_MAX_PAGES + USER_MAX_STACK_PAGES + 2U)
#define USER_HEAP_START (USER_CODE_ADDRESS + UINT64_C(0x00400000))
/* Upstream C++ ABI archives make otherwise small utility ELFs span well over
 * the old 128-page (512 KiB) image ceiling. Keep the cap below the 16 MiB
 * address window while allowing normal exception-enabled static executables. */
#define USER_IMAGE_MAX_PAGES 512U
#define USER_IMAGE_LOAD_WINDOW UINT64_C(0x01000000)
#define USER_DYNAMIC_RELOCATION_PAGE_COUNT \
    (USER_IMAGE_LOAD_WINDOW / PAGE_SIZE)
#define USER_DYNAMIC_RELOCATION_BITMAP_WORDS \
    ((USER_DYNAMIC_RELOCATION_PAGE_COUNT + 63U) / 64U)
/* Real upstream text/font stacks allocate several MiB of transient font
 * configuration, font-file blobs, shaping, and glyph data. Keep the 32-bit
 * process-table cap conservative, while giving 64-bit ports a 16 MiB brk heap
 * per process so HarfBuzz can use its file-read fallback when mmap is full. */
#if defined(__x86_64__) || defined(__aarch64__) || \
    (defined(__riscv) && __riscv_xlen == 64)
#define USER_MAX_HEAP_PAGES 8192U
#else
#define USER_MAX_HEAP_PAGES 256U
#endif
/* Large upstream desktop runtimes need a wider per-process mapped-image
 * budget than the native shell. Keep the larger allowance x86_64-specific
 * until the remaining ports have equivalent loader and memory validation. */
#if defined(__x86_64__)
#define USER_MAX_MMAP_PAGES 16384U
#elif defined(__riscv)
#define USER_MAX_MMAP_PAGES 2048U
#else
#define USER_MAX_MMAP_PAGES 1024U
#endif
#define PROCESS_SHM_MAX_OBJECTS 16U
#define MMAP_BACKING_SHARED UINT32_C(1)
#define MMAP_BACKING_MEMFD UINT32_C(2)
/* An immutable VFS file may be mapped MAP_SHARED for read access. Its data is
 * snapshotted into a read-only backing because VFS files have no page cache. */
#define MMAP_BACKING_IMMUTABLE_FILE UINT32_C(4)
#if defined(__arm__)
/* ARMv7's user page tables cover 0x10000000..0x10ffffff.  Keep mappings
 * below the signal trampoline at 0x10f00000 and above the heap. */
#define USER_MMAP_START (USER_CODE_ADDRESS + UINT64_C(0x00600000))
#define USER_MMAP_END   (USER_CODE_ADDRESS + UINT64_C(0x00e00000))
#elif defined(__x86_64__)
/* Real toolkit dependency graphs occupy tens of MiB of mapped ELF segments.
 * Keep them above the executable, heap, and signal trampoline while leaving
 * a 256 MiB virtual window for the 64 MiB per-process mmap page budget. */
#define USER_MMAP_START (USER_CODE_ADDRESS + UINT64_C(0x03000000))
#define USER_MMAP_END   (USER_CODE_ADDRESS + UINT64_C(0x13000000))
#else
#define USER_MMAP_START (USER_CODE_ADDRESS + UINT64_C(0x02000000))
#define USER_MMAP_END   (USER_CODE_ADDRESS + UINT64_C(0x03000000))
#endif
#define USER_MAX_PROCESSES 64U
#define USER_FORK_CONTEXT_SIZE 256U
#if defined(__aarch64__)
#define USER_FORK_CONTEXT_ALLOCATION_SIZE 280U
#else
#define USER_FORK_CONTEXT_ALLOCATION_SIZE USER_FORK_CONTEXT_SIZE
#endif
#if defined(__x86_64__) || defined(__aarch64__) || \
    (defined(__riscv) && __riscv_xlen == 64)
/* The 64-bit ports reserve a 32 MiB brk heap beginning 4 MiB above the user
 * image. Keep the signal trampoline beyond that full 36 MiB heap window. */
#define USER_SIGNAL_TRAMPOLINE_OFFSET UINT64_C(0x02800000)
#else
#define USER_SIGNAL_TRAMPOLINE_OFFSET UINT64_C(0x00f00000)
#endif
#define USER_SIGNAL_TRAMPOLINE_ADDRESS \
    (USER_CODE_ADDRESS + USER_SIGNAL_TRAMPOLINE_OFFSET)

#if defined(__x86_64__) || defined(__aarch64__) || \
    (defined(__riscv) && __riscv_xlen == 64)
_Static_assert(USER_HEAP_START +
                   (uint64_t)USER_MAX_HEAP_PAGES * PAGE_SIZE <=
                   USER_SIGNAL_TRAMPOLINE_ADDRESS,
               "64-bit process heap must not overlap its signal trampoline");
_Static_assert(USER_SIGNAL_TRAMPOLINE_ADDRESS + PAGE_SIZE <=
                   USER_MMAP_START,
               "signal trampoline must remain below the mmap arena");
#endif

#if defined(__i386__)
static const uint8_t signal_exit_trampoline[] = {
    0xb8, 0x02, 0x00, 0x00, 0x00, /* mov $2, %eax */
    0xcd, 0x80                    /* int $0x80 */
};
#elif defined(__arm__)
static const uint8_t signal_exit_trampoline[] = {
    0x02, 0x70, 0xa0, 0xe3, /* mov r7, #2 */
    0x00, 0x00, 0x00, 0xef  /* svc #0 */
};
#elif defined(__aarch64__)
static const uint8_t signal_exit_trampoline[] = {
    0x08, 0x01, 0x80, 0xd2, /* mov x8, #2 */
    0x01, 0x00, 0x00, 0xd4  /* svc #0 */
};
#elif defined(__riscv)
static const uint8_t signal_exit_trampoline[] = {
    0x93, 0x08, 0x20, 0x00, /* addi a7, zero, 2 */
    0x73, 0x00, 0x00, 0x00  /* ecall */
};
#else
static const uint8_t signal_exit_trampoline[] = {
    0x48, 0xc7, 0xc0, 0x02, 0x00, 0x00, 0x00, /* mov $2, %rax */
    0xcd, 0x80                                      /* int $0x80 */
};
#endif

#define ELF_PT_LOAD 1U
#define ELF_PT_DYNAMIC 2U
#define ELF_PT_INTERP 3U
#define ELF_PT_PHDR 6U
#define ELF_PF_X 1U
#define ELF_PF_W 2U
#define ELF_AT_NULL 0U
#define ELF_AT_PHDR 3U
#define ELF_AT_PHENT 4U
#define ELF_AT_PHNUM 5U
#define ELF_AT_BASE 7U
#define ELF_AT_FLAGS 8U
#define ELF_AT_ENTRY 9U
#define ELF_AT_UID 11U
#define ELF_AT_EUID 12U
#define ELF_AT_GID 13U
#define ELF_AT_EGID 14U
#define ELF_AT_PAGESZ 6U
#define ELF_AT_SECURE 23U
#define ELF_AT_EXECFN 31U
#define ELF_AT_AKAOS_INTERPRETER 0x6f6b0001U
#define ELF_INTERPRETER_PATH_MAX 128U

struct elf32_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

struct elf32_dynamic {
    int32_t tag;
    uint32_t value;
};

struct elf64_dynamic {
    int64_t tag;
    uint64_t value;
};

struct elf32_rel {
    uint32_t offset;
    uint32_t info;
};

struct elf32_rela {
    uint32_t offset;
    uint32_t info;
    int32_t addend;
};

struct elf64_rel {
    uint64_t offset;
    uint64_t info;
};

struct elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

#define ELF_DT_NULL 0
#define ELF_DT_NEEDED 1
#define ELF_DT_PLTRELSZ 2
#define ELF_DT_PLTGOT 3
#define ELF_DT_HASH 4
#define ELF_DT_STRTAB 5
#define ELF_DT_SYMTAB 6
#define ELF_DT_RELA 7
#define ELF_DT_RELASZ 8
#define ELF_DT_RELAENT 9
#define ELF_DT_STRSZ 10
#define ELF_DT_SYMENT 11
#define ELF_DT_INIT 12
#define ELF_DT_FINI 13
#define ELF_DT_REL 17
#define ELF_DT_RELSZ 18
#define ELF_DT_RELENT 19
#define ELF_DT_PLTREL 20
#define ELF_DT_DEBUG 21
#define ELF_DT_JMPREL 23
#define ELF_DT_FLAGS 30
#define ELF_DT_RELCOUNT UINT64_C(0x6ffffffa)
#define ELF_DT_RELACOUNT UINT64_C(0x6ffffff9)
#define ELF_DT_FLAGS_1 UINT64_C(0x6ffffffb)
#define ELF_DT_GNU_HASH UINT64_C(0x6ffffef5)

struct process_dynamic_metadata {
    uint32_t enabled;
    uint64_t rela_vaddr;
    uint64_t rela_size;
    uint64_t rela_ent;
    uint64_t rel_vaddr;
    uint64_t rel_size;
    uint64_t rel_ent;
    uint64_t relocation_page_bitmap[USER_DYNAMIC_RELOCATION_BITMAP_WORDS];
};

struct process_auxiliary_metadata {
    uint64_t program_header;
    uint64_t program_header_entry_size;
    uint64_t program_header_count;
    uint64_t interpreter_base;
    uint64_t interpreter_present;
    uint64_t entry;
};

struct process_auxiliary_pair {
    uint64_t type;
    uint64_t value;
};

struct process_image_backing {
    const uint8_t *image;
    size_t size;
    uint64_t load_base;
    struct process_dynamic_metadata dynamic;
};

static void process_dynamic_metadata_copy(
    struct process_dynamic_metadata *destination,
    const struct process_dynamic_metadata *source) {
    destination->enabled = source->enabled;
    destination->rela_vaddr = source->rela_vaddr;
    destination->rela_size = source->rela_size;
    destination->rela_ent = source->rela_ent;
    destination->rel_vaddr = source->rel_vaddr;
    destination->rel_size = source->rel_size;
    destination->rel_ent = source->rel_ent;
    for (size_t index = 0;
         index < USER_DYNAMIC_RELOCATION_BITMAP_WORDS; ++index) {
        destination->relocation_page_bitmap[index] =
            source->relocation_page_bitmap[index];
    }
}

struct process_mmap_backing {
    uint32_t references;
    uint32_t flags;
    uint64_t length;
    uint32_t seals;
    uint32_t sealable;
    uint64_t physical_pages[USER_MAX_MMAP_PAGES];
    uint8_t data[];
};

struct process_shm_object {
    uint32_t in_use;
    uint32_t linked;
    uint32_t descriptor_references;
    uint32_t owner_uid;
    uint32_t owner_gid;
    uint32_t mode;
    char name[AKAOS_SHM_NAME_MAX + 1];
    struct process_mmap_backing *backing;
};

#define USER_PROCESS_CONTEXT_ALLOCATION_SIZE \
    (USER_FORK_CONTEXT_ALLOCATION_SIZE + sizeof(struct process_image_backing))

struct xscore_process {
    /* Process state is touched by the owning user thread, waiters, and (on
     * x86 SMP) the process table observer on another CPU. */
    xscore_spinlock_t lock;
    uint64_t pid;
    uint64_t thread_id;
    uint64_t waiter_thread_id;
    uint64_t address_space_root;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t stack_bottom;
    uint64_t mapped_pages[USER_TRACKED_PAGE_CAPACITY];
    uint64_t physical_pages[USER_TRACKED_PAGE_CAPACITY];
    uint64_t mapped_page_flags[USER_TRACKED_PAGE_CAPACITY];
    uint8_t mapped_page_cow[USER_TRACKED_PAGE_CAPACITY];
    uint32_t mapped_page_count;
    uint64_t heap_start;
    uint64_t heap_break;
    uint64_t heap_physical_pages[USER_MAX_HEAP_PAGES];
    uint8_t heap_page_cow[USER_MAX_HEAP_PAGES];
    uint32_t heap_page_count;
    uint64_t mmap_pages[USER_MAX_MMAP_PAGES];
    uint64_t mmap_physical_pages[USER_MAX_MMAP_PAGES];
    uint64_t mmap_page_flags[USER_MAX_MMAP_PAGES];
    uint8_t mmap_protections[USER_MAX_MMAP_PAGES];
    uint8_t mmap_page_cow[USER_MAX_MMAP_PAGES];
    struct process_mmap_backing *mmap_backings[USER_MAX_MMAP_PAGES];
    uint64_t mmap_backing_offsets[USER_MAX_MMAP_PAGES];
    uint32_t mmap_page_count;
    uint64_t mmap_next_address;
    volatile enum xscore_process_state state;
    uint64_t exit_status;
    uint32_t termination_signal;
    uint32_t wait_blocked;
    uint64_t parent_pid;
    uint64_t pgid;
    uint64_t sid;
    uint32_t controlling_tty;
    volatile uint32_t pending_signal;
    int32_t pending_signal_code;
    uint64_t pending_signal_address;
    uint64_t pending_signal_sender_pid;
    uint32_t pending_signal_sender_uid;
    uint32_t ignored_signals;
    uint32_t signal_mask;
    struct akaos_sigaction signal_actions[AKAOS_SIGNAL_MAX + 1];
    uint32_t foreground;
    uint64_t signal_trampoline;
    uint32_t uid;
    uint32_t gid;
    uint32_t effective_uid;
    uint32_t effective_gid;
    uint32_t saved_uid;
    uint32_t saved_gid;
    char cwd[128];
    uint32_t cwd_node;
    uint32_t image_node_id;
    uint32_t image_node_ref;
    char name[32];
    uint8_t *fork_context;
    uint32_t fork_context_size;
    uint32_t fork_resume;
    /* Keep the architectural fork/exec resume frame resident with the
     * process object.  A heap-owned frame could be reclaimed while a reused
     * TCB was still becoming runnable, leaving AArch64 xscore_user_resume with
     * a null context after long compositor sessions. */
    uint8_t fork_context_storage[USER_PROCESS_CONTEXT_ALLOCATION_SIZE];
    void *last_syscall_frame;
    struct xscore_fd_table file_descriptors;
};

static struct xscore_process processes[USER_MAX_PROCESSES];
static struct process_shm_object shm_objects[PROCESS_SHM_MAX_OBJECTS];
static uint64_t next_pid;
static uint64_t process_reaper_pid;
static uint64_t tty_foreground_pgid;
static uint64_t tty_shell_pgid;
/* The process table is shared kernel state.  A slot is reserved under this
 * lock before its potentially slow address-space, ELF, and descriptor setup
 * starts, then published only once it is ready to schedule. */
static xscore_spinlock_t process_table_lock = XSCORE_SPINLOCK_INIT;
static uint32_t next_user_ap = 1;
static volatile int multicore_userspace_enabled;
static uint32_t process_runtime_flags;
#define PROCESS_RUNTIME_INITIALIZED UINT32_C(1)
#define PROCESS_RUNTIME_ANON_REPORTED UINT32_C(2)
#define PROCESS_RUNTIME_COW_REPORTED UINT32_C(4)
#define PROCESS_RUNTIME_FILE_REPORTED UINT32_C(8)

static enum xscore_process_state process_state_snapshot(
    const struct xscore_process *process);

static int process_is_visible(const struct xscore_process *process) {
    if (!process) return 0;
    enum xscore_process_state state = process_state_snapshot(process);
    return state == XSCORE_PROCESS_READY || state == XSCORE_PROCESS_RUNNING ||
           state == XSCORE_PROCESS_STOPPED || state == XSCORE_PROCESS_EXITED;
}

static int process_state_is_visible(enum xscore_process_state state) {
    return state == XSCORE_PROCESS_READY ||
           state == XSCORE_PROCESS_RUNNING ||
           state == XSCORE_PROCESS_STOPPED ||
           state == XSCORE_PROCESS_EXITED;
}

static struct xscore_process *process_slot_reserve(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&process_table_lock);
    if (!(process_runtime_flags & PROCESS_RUNTIME_INITIALIZED)) {
        xscore_spin_unlock_irqrestore(&process_table_lock, flags);
        return 0;
    }
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        if (processes[index].state == XSCORE_PROCESS_UNUSED ||
            processes[index].state == XSCORE_PROCESS_REAPED) {
            struct xscore_process *process = &processes[index];
            process->state = XSCORE_PROCESS_ALLOCATING;
            /* A slot is reusable only after its previous thread and image
             * have been fully reaped.  Clear the transition fields at the
             * reservation boundary so a new direct spawn can never inherit
             * fork-resume state from the previous occupant. */
            process->thread_id = 0;
            process->waiter_thread_id = 0;
            process->address_space_root = 0;
            process->entry = 0;
            process->stack_top = 0;
            process->stack_bottom = 0;
            process->fork_context = 0;
            process->fork_context_size = 0;
            process->fork_resume = 0;
            process->last_syscall_frame = 0;
            process->mapped_page_count = 0;
            process->heap_page_count = 0;
            process->mmap_page_count = 0;
            process->mmap_next_address = USER_MMAP_START;
            process->image_node_id = 0;
            process->image_node_ref = 0;
            process->pid = next_pid++;
            xscore_spin_unlock_irqrestore(&process_table_lock, flags);
            return process;
        }
    }
    xscore_spin_unlock_irqrestore(&process_table_lock, flags);
    return 0;
}

static void process_slot_release(struct xscore_process *process) {
    if (!process) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&process_table_lock);
    process->state = XSCORE_PROCESS_UNUSED;
    xscore_spin_unlock_irqrestore(&process_table_lock, flags);
}

static void process_image_release(struct xscore_process *process) {
    if (!process || !process->image_node_ref) return;
    xscore_vfs_node_release(process->image_node_id);
    process->image_node_id = 0;
    process->image_node_ref = 0;
}

static enum xscore_process_state process_state_snapshot(
    const struct xscore_process *process) {
    if (!process) return XSCORE_PROCESS_UNUSED;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    enum xscore_process_state state = process->state;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return state;
}

static void process_slot_publish(struct xscore_process *process) {
    if (!process) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&process_table_lock);
    if (process->state == XSCORE_PROCESS_ALLOCATING) {
        process->state = XSCORE_PROCESS_READY;
    }
    xscore_spin_unlock_irqrestore(&process_table_lock, flags);
}

static void process_slot_mark_reaped(struct xscore_process *process) {
    if (!process) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&process_table_lock);
    process->state = XSCORE_PROCESS_REAPED;
    xscore_spin_unlock_irqrestore(&process_table_lock, flags);
}

int xscore_process_set_reaper(struct xscore_process *process) {
    if (!(process_runtime_flags & PROCESS_RUNTIME_INITIALIZED) || !process)
        return 0;

    uintptr_t table_flags = xscore_spin_lock_irqsave(&process_table_lock);
    uintptr_t process_flags = xscore_spin_lock_irqsave(&process->lock);
    enum xscore_process_state state = process->state;
    uint64_t pid = process->pid;
    xscore_spin_unlock_irqrestore(&process->lock, process_flags);
    if (!pid || (state != XSCORE_PROCESS_READY &&
                 state != XSCORE_PROCESS_RUNNING &&
                 state != XSCORE_PROCESS_STOPPED)) {
        xscore_spin_unlock_irqrestore(&process_table_lock, table_flags);
        return 0;
    }

    process_reaper_pid = pid;
    /* If init was restarted, children temporarily detached while there was
     * no live reaper are adopted by this newly published supervisor. */
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        struct xscore_process *child = &processes[index];
        if (child == process) continue;
        process_flags = xscore_spin_lock_irqsave(&child->lock);
        if (process_state_is_visible(child->state) &&
            child->parent_pid == 0) {
            child->parent_pid = pid;
        }
        xscore_spin_unlock_irqrestore(&child->lock, process_flags);
    }
    xscore_spin_unlock_irqrestore(&process_table_lock, table_flags);
    return 1;
}

static struct process_image_backing *process_image_backing(
    const struct xscore_process *process) {
    if (!process || !process->fork_context) return 0;
    return (struct process_image_backing *)(void *)(
        process->fork_context + USER_FORK_CONTEXT_ALLOCATION_SIZE);
}

static int process_context_allocate(struct xscore_process *process) {
    if (!process) return 0;
    process->fork_context = process->fork_context_storage;
    for (uint32_t index = 0;
         index < USER_PROCESS_CONTEXT_ALLOCATION_SIZE; ++index) {
        process->fork_context_storage[index] = 0;
    }
    return 1;
}

static void process_context_release(struct xscore_process *process) {
    if (!process) return;
    if (process->fork_context &&
        process->fork_context != process->fork_context_storage) {
        (void)xscore_heap_free(process->fork_context);
    }
    process->fork_context = 0;
    process->fork_context_size = 0;
}

/* Keep the console owner on the BSP while distributing descendants across
 * online APs.  Descendants of an AP stay local to that AP. */
static uint32_t process_default_target_cpu(void) {
    uint32_t online = xscore_smp_cpu_count();
    if (!multicore_userspace_enabled || online <= 1 ||
        xscore_smp_current_cpu_index() != 0) {
        return SMP_MAX_CPUS;
    }
    uint32_t slot = __atomic_fetch_add(&next_user_ap, 1U, __ATOMIC_RELAXED);
    return 1U + (slot % (online - 1U));
}

void xscore_process_enable_multicore_userspace(int enabled) {
#if defined(__x86_64__)
    multicore_userspace_enabled = enabled != 0;
#else
    /* Non-x86 trap-return migration is not enabled until its per-CPU
     * exception-frame and address-space handoff tests are complete. */
    (void)enabled;
    multicore_userspace_enabled = 0;
#endif
}

void xscore_process_capture_syscall_context(uint32_t number,
                                            const void *frame,
                                            uint64_t captured_elr,
                                            uint64_t captured_sp_el0,
                                            uint64_t captured_spsr) {
    (void)number;
#if !defined(__aarch64__)
    (void)captured_elr;
    (void)captured_sp_el0;
    (void)captured_spsr;
#endif
    if (!frame) return;
    struct xscore_process *process = xscore_process_current();
    if (!process) return;
    process->last_syscall_frame = (void *)frame;
    if (!process->fork_context) {
        if (!process_context_allocate(process)) return;
        struct process_image_backing *backing = process_image_backing(process);
        backing->image = 0;
        backing->size = 0;
        backing->load_base = 0;
    }
#if defined(__x86_64__)
    const uint32_t size = 20U * sizeof(uint64_t);
#elif defined(__i386__)
    const uint32_t size = 11U * sizeof(uint32_t);
#elif defined(__aarch64__)
    const uint32_t size = 32U * sizeof(uint64_t);
#elif defined(__arm__)
    const uint32_t size = 15U * sizeof(uint32_t);
#elif defined(__riscv) && __riscv_xlen == 64
    const uint32_t size = 32U * sizeof(uint64_t);
#elif defined(__riscv)
    const uint32_t size = 32U * sizeof(uint32_t);
#else
    const uint32_t size = 0;
#endif
    if (!size || size > USER_FORK_CONTEXT_SIZE) return;
    const uint8_t *source = (const uint8_t *)frame;
    for (uint32_t i = 0; i < size; ++i) process->fork_context[i] = source[i];
    process->fork_context_size = size;
#if defined(__aarch64__)
    ((uint64_t *)process->fork_context)[32] = captured_elr;
    ((uint64_t *)process->fork_context)[33] = captured_sp_el0;
    ((uint64_t *)process->fork_context)[34] = captured_spsr;
#elif defined(__arm__)
    struct xscore_user_transition *transition =
        xscore_thread_current_user_transition();
    ((uint32_t *)process->fork_context)[15] =
        transition ? (uint32_t)transition->user_rsp : 0;
    process->fork_context_size = 16U * sizeof(uint32_t);
#endif
}

static void zero_page(uint64_t address) {
    uint64_t *words = (uint64_t *)(uintptr_t)address;
    for (size_t index = 0; index < PAGE_SIZE / sizeof(uint64_t); ++index) {
        words[index] = 0;
    }
}

static uint64_t align_down(uint64_t value) {
    return value & ~(PAGE_SIZE - 1);
}

static int range_inside(uint64_t offset, uint64_t length, uint64_t total) {
    return offset <= total && length <= total - offset;
}

static void serial_process_status(const char *prefix, uint64_t pid,
                                  const char *suffix) {
    char line[96];
    char digits[21];
    uint32_t count = 0;
    char *out = line;
    while (*prefix) *out++ = *prefix++;
    do {
        digits[count++] = (char)('0' + (pid % 10));
        pid /= 10;
    } while (pid != 0);
    while (count > 0) *out++ = digits[--count];
    while (*suffix) *out++ = *suffix++;
    *out = '\0';
    xscore_serial_write(line);
}


/* Name the process that faulted.
 *
 * The name is the first thing a reader of a fault report looks for, and the
 * layout of the address space that follows it is long, so the name is
 * printed on its own before the layout. */
void xscore_process_report_name(void) {
    struct xscore_process *process = xscore_process_current();
    if (!process) {
        xscore_serial_write("[FATAL] The fault happened outside of a process\n");
        return;
    }
    serial_process_status("[FATAL] Faulting process ", process->pid, "\n");
    xscore_serial_write("[FATAL] Faulting process name: ");
    xscore_serial_write(process->name[0] ? process->name : "(unnamed)");
    xscore_serial_write("\n");
}

/* Describe the address space of the process that faulted.
 *
 * A fault inside a user process is almost always a mistake in that process,
 * and the name of the process together with the layout of its address space
 * is what tells the two apart. */
void xscore_process_report_fault(uint64_t fault_address) {
    struct xscore_process *process = xscore_process_current();
    if (!process) {
        xscore_serial_write("[FATAL] The fault happened outside of a process\n");
        return;
    }
    serial_process_status("[FATAL] Faulting process ", process->pid, "\n");
    xscore_serial_write("[FATAL] Faulting process name: ");
    xscore_serial_write(process->name[0] ? process->name : "(unnamed)");
    xscore_serial_write("\n");
    serial_process_status("[FATAL] Faulting process stack bottom: ",
                          process->stack_bottom, "\n");
    serial_process_status("[FATAL] Faulting process stack top: ",
                          process->stack_top, "\n");
    serial_process_status("[FATAL] Faulting process heap start: ",
                          process->heap_start, "\n");
    serial_process_status("[FATAL] Faulting process heap break: ",
                          process->heap_break, "\n");
    serial_process_status("[FATAL] Faulting process mmap next: ",
                          process->mmap_next_address, "\n");
    serial_process_status("[FATAL] Faulting process mapped pages: ",
                          process->mapped_page_count, "\n");
    (void)fault_address;
}

static void mmap_backing_acquire(struct process_mmap_backing *backing) {
    if (backing) ++backing->references;
}

static void mmap_backing_release(struct process_mmap_backing *backing) {
    if (!backing || backing->references == 0) return;
    --backing->references;
    if (backing->references == 0) {
        if (backing->flags & MMAP_BACKING_SHARED) {
            for (uint32_t i = 0; i < USER_MAX_MMAP_PAGES; ++i) {
                if (backing->physical_pages[i]) {
                    (void)xscore_pmm_refcount_dec(backing->physical_pages[i]);
                    backing->physical_pages[i] = 0;
                }
            }
        }
        (void)xscore_heap_free(backing);
    }
}

static struct process_mmap_backing *mmap_backing_create(uint64_t length,
                                                        uint32_t flags,
                                                        int with_snapshot) {
    size_t extra = with_snapshot ? (size_t)length : 0;
    struct process_mmap_backing *backing =
        (struct process_mmap_backing *)xscore_heap_allocate(
            sizeof(*backing) + extra);
    if (!backing) return 0;
    backing->references = 0;
    backing->flags = flags;
    backing->length = length;
    backing->seals = 0;
    backing->sealable = 0;
    for (uint32_t i = 0; i < USER_MAX_MMAP_PAGES; ++i) {
        backing->physical_pages[i] = 0;
    }
    if (with_snapshot) {
        for (uint64_t i = 0; i < length; ++i) backing->data[i] = 0;
    }
    return backing;
}

static void shm_object_clear(struct process_shm_object *object) {
    if (!object) return;
    mmap_backing_release(object->backing);
    object->in_use = 0;
    object->linked = 0;
    object->descriptor_references = 0;
    object->owner_uid = 0;
    object->owner_gid = 0;
    object->mode = 0;
    object->name[0] = 0;
    object->backing = 0;
}

static void shm_object_maybe_destroy(struct process_shm_object *object) {
    if (object && object->in_use && !object->linked &&
        object->descriptor_references == 0) {
        shm_object_clear(object);
    }
}

static void release_process_pages(struct xscore_process *process) {
    for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
        if (process->physical_pages[index]) {
            (void)xscore_vmm_unmap_page_in(process->address_space_root,
                                           process->mapped_pages[index], 0);
            (void)xscore_pmm_refcount_dec(process->physical_pages[index]);
        }
        process->mapped_pages[index] = 0;
        process->physical_pages[index] = 0;
        process->mapped_page_flags[index] = 0;
        process->mapped_page_cow[index] = 0;
    }
    process->mapped_page_count = 0;
    struct process_image_backing *backing = process_image_backing(process);
    if (backing) {
        backing->image = 0;
        backing->size = 0;
        backing->load_base = 0;
    }

    for (uint32_t index = 0; index < process->heap_page_count; ++index) {
        if (process->heap_physical_pages[index]) {
            (void)xscore_vmm_unmap_page_in(process->address_space_root,
                                           process->heap_start +
                                               (uint64_t)index * PAGE_SIZE,
                                           0);
            (void)xscore_pmm_refcount_dec(process->heap_physical_pages[index]);
        }
        process->heap_physical_pages[index] = 0;
        process->heap_page_cow[index] = 0;
    }
    process->heap_page_count = 0;
    process->heap_break = process->heap_start;

    for (uint32_t index = 0; index < process->mmap_page_count; ++index) {
        if (process->mmap_physical_pages[index]) {
            (void)xscore_vmm_unmap_page_in(process->address_space_root,
                                           process->mmap_pages[index], 0);
            (void)xscore_pmm_refcount_dec(
                process->mmap_physical_pages[index]);
        }
        mmap_backing_release(process->mmap_backings[index]);
        process->mmap_pages[index] = 0;
        process->mmap_physical_pages[index] = 0;
        process->mmap_page_flags[index] = 0;
        process->mmap_protections[index] = 0;
        process->mmap_page_cow[index] = 0;
        process->mmap_backings[index] = 0;
        process->mmap_backing_offsets[index] = 0;
    }
    process->mmap_page_count = 0;
    process->mmap_next_address = USER_MMAP_START;
}

static int process_track_page(struct xscore_process *process,
                              uint64_t virtual_address,
                              uint64_t physical_address,
                              uint64_t flags) {
    for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
        if (process->mapped_pages[index] == virtual_address) {
            process->physical_pages[index] = physical_address;
            process->mapped_page_flags[index] = XSCORE_PAGE_USER | flags;
            process->mapped_page_cow[index] = 0;
            return 1;
        }
    }
    if (process->mapped_page_count >= USER_TRACKED_PAGE_CAPACITY) return 0;
    process->mapped_pages[process->mapped_page_count] = virtual_address;
    process->physical_pages[process->mapped_page_count] = physical_address;
    process->mapped_page_flags[process->mapped_page_count] =
        XSCORE_PAGE_USER | flags;
    process->mapped_page_cow[process->mapped_page_count] = 0;
    ++process->mapped_page_count;
    return 1;
}

static int process_reserve_image_page(struct xscore_process *process,
                                      uint64_t virtual_address,
                                      uint64_t flags) {
    for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
        if (process->mapped_pages[index] != virtual_address) continue;
        uint64_t old_flags = process->mapped_page_flags[index];
        uint64_t merged = old_flags | (flags & XSCORE_PAGE_WRITABLE);
        if ((old_flags & XSCORE_PAGE_NO_EXECUTE) &&
            (flags & XSCORE_PAGE_NO_EXECUTE)) {
            merged |= XSCORE_PAGE_NO_EXECUTE;
        } else {
            merged &= ~XSCORE_PAGE_NO_EXECUTE;
        }
        process->mapped_page_flags[index] = XSCORE_PAGE_USER | merged;
        return 1;
    }
    return process_track_page(process, virtual_address, 0, flags);
}

static int process_map_new_page(struct xscore_process *process,
                                uint64_t virtual_address, uint64_t flags,
                                uint64_t *physical_out) {
    uint64_t physical = xscore_pmm_allocate_page();
    if (!physical) return 0;
    if (!xscore_vmm_map_page_in(process->address_space_root, virtual_address,
                                physical, XSCORE_PAGE_USER | flags)) {
        xscore_pmm_free_page(physical);
        return 0;
    }
    if (!process_track_page(process, virtual_address, physical, flags)) {
        (void)xscore_vmm_unmap_page_in(process->address_space_root,
                                       virtual_address, 0);
        xscore_pmm_free_page(physical);
        return 0;
    }
    zero_page(physical);
    if (physical_out) *physical_out = physical;
    return 1;
}

static size_t process_build_auxiliary_vector(
    const struct xscore_process *process,
    const struct process_auxiliary_metadata *metadata,
    uint64_t execfn,
    struct process_auxiliary_pair *pairs, size_t capacity) {
    if (!pairs || capacity < 1) return 0;
    size_t count = 0;
#define ADD_AUXILIARY(type_value, value_value) do { \
        if (count >= capacity) return 0; \
        pairs[count].type = (type_value); \
        pairs[count].value = (value_value); \
        ++count; \
    } while (0)
    if (metadata && metadata->program_header != 0) {
        ADD_AUXILIARY(ELF_AT_PHDR, metadata->program_header);
        ADD_AUXILIARY(ELF_AT_PHENT, metadata->program_header_entry_size);
        ADD_AUXILIARY(ELF_AT_PHNUM, metadata->program_header_count);
    }
    ADD_AUXILIARY(ELF_AT_PAGESZ, PAGE_SIZE);
    ADD_AUXILIARY(ELF_AT_BASE, metadata ? metadata->interpreter_base : 0);
    if (metadata && metadata->interpreter_present)
        ADD_AUXILIARY(ELF_AT_AKAOS_INTERPRETER, 1U);
    ADD_AUXILIARY(ELF_AT_FLAGS, 0);
    ADD_AUXILIARY(ELF_AT_ENTRY, metadata ? metadata->entry : 0);
    ADD_AUXILIARY(ELF_AT_UID, process ? process->uid : 0);
    ADD_AUXILIARY(ELF_AT_EUID, process ? process->effective_uid : 0);
    ADD_AUXILIARY(ELF_AT_GID, process ? process->gid : 0);
    ADD_AUXILIARY(ELF_AT_EGID, process ? process->effective_gid : 0);
    ADD_AUXILIARY(ELF_AT_SECURE, 0);
    ADD_AUXILIARY(ELF_AT_EXECFN, execfn);
    ADD_AUXILIARY(ELF_AT_NULL, 0);
#undef ADD_AUXILIARY
    return count;
}

static int setup_user_stack_arguments(struct xscore_process *process,
                                      uint64_t stack_physical,
                                      int argc,
                                      const char *const argv[], int envc,
                                      const char *const envp[],
                                      const char *exec_path,
                                      const struct process_auxiliary_metadata *auxiliary) {
    if (argc < 0 || argc > 32 || envc < 0 || envc > 32) return 0;
    uint8_t *stack_mem = (uint8_t *)(uintptr_t)stack_physical;
    size_t string_cur = PAGE_SIZE;

#if defined(__i386__) || defined(__arm__) || (defined(__riscv) && __riscv_xlen == 32)
    uint32_t user_argv_ptrs[33];
    uint32_t user_env_ptrs[33];
    for (int i = 0; i < argc; ++i) {
        if (!argv || !argv[i]) return 0;
        size_t len = 0;
        while (argv[i][len]) ++len;
        size_t total_len = len + 1;
        if (string_cur < total_len) return 0;
        string_cur -= total_len;
        for (size_t c = 0; c <= len; ++c) {
            stack_mem[string_cur + c] = (uint8_t)argv[i][c];
        }
        user_argv_ptrs[i] = (uint32_t)(USER_STACK_PAGE + string_cur);
    }
    user_argv_ptrs[argc] = 0;

    uint32_t user_execfn = argc > 0 ? user_argv_ptrs[0] : 0;
    if (exec_path && *exec_path) {
        size_t len = 0;
        while (exec_path[len]) ++len;
        size_t total_len = len + 1;
        if (string_cur < total_len) return 0;
        string_cur -= total_len;
        for (size_t c = 0; c <= len; ++c) {
            stack_mem[string_cur + c] = (uint8_t)exec_path[c];
        }
        user_execfn = (uint32_t)(USER_STACK_PAGE + string_cur);
    }

    for (int i = 0; i < envc; ++i) {
        if (!envp || !envp[i]) return 0;
        size_t len = 0;
        while (envp[i][len]) ++len;
        size_t total_len = len + 1;
        if (string_cur < total_len) return 0;
        string_cur -= total_len;
        for (size_t c = 0; c <= len; ++c) {
            stack_mem[string_cur + c] = (uint8_t)envp[i][c];
        }
        user_env_ptrs[i] = (uint32_t)(USER_STACK_PAGE + string_cur);
    }
    user_env_ptrs[envc] = 0;

    struct process_auxiliary_pair auxiliary_pairs[16];
    size_t auxiliary_count = process_build_auxiliary_vector(
        process, auxiliary, user_execfn, auxiliary_pairs,
        sizeof(auxiliary_pairs) / sizeof(auxiliary_pairs[0]));
    if (auxiliary_count == 0) return 0;
    size_t table_slots = 1 + (size_t)(argc + 1) +
                         (size_t)(envc + 1) + auxiliary_count * 2;
    size_t table_bytes = table_slots * sizeof(uint32_t);
    if (string_cur < table_bytes) return 0;

    size_t table_offset = (string_cur - table_bytes) & ~(size_t)15;
    uint32_t *table = (uint32_t *)(uintptr_t)(stack_mem + table_offset);

    table[0] = (uint32_t)argc;
    for (int i = 0; i <= argc; ++i) {
        table[1 + i] = user_argv_ptrs[i];
    }
    for (int i = 0; i <= envc; ++i) {
        table[1 + argc + 1 + i] = user_env_ptrs[i];
    }
    size_t aux_offset = 1 + (size_t)argc + 1 + (size_t)envc + 1;
    for (size_t index = 0; index < auxiliary_count; ++index) {
        table[aux_offset + index * 2] = (uint32_t)auxiliary_pairs[index].type;
        table[aux_offset + index * 2 + 1] =
            (uint32_t)auxiliary_pairs[index].value;
    }

    process->stack_top = USER_STACK_PAGE + table_offset;
    return 1;
#else
    uint64_t user_argv_ptrs[33];
    uint64_t user_env_ptrs[33];
    for (int i = 0; i < argc; ++i) {
        if (!argv || !argv[i]) return 0;
        size_t len = 0;
        while (argv[i][len]) ++len;
        size_t total_len = len + 1;
        if (string_cur < total_len) return 0;
        string_cur -= total_len;
        for (size_t c = 0; c <= len; ++c) {
            stack_mem[string_cur + c] = (uint8_t)argv[i][c];
        }
        user_argv_ptrs[i] = USER_STACK_PAGE + string_cur;
    }
    user_argv_ptrs[argc] = 0;

    uint64_t user_execfn = argc > 0 ? user_argv_ptrs[0] : 0;
    if (exec_path && *exec_path) {
        size_t len = 0;
        while (exec_path[len]) ++len;
        size_t total_len = len + 1;
        if (string_cur < total_len) return 0;
        string_cur -= total_len;
        for (size_t c = 0; c <= len; ++c) {
            stack_mem[string_cur + c] = (uint8_t)exec_path[c];
        }
        user_execfn = USER_STACK_PAGE + string_cur;
    }

    for (int i = 0; i < envc; ++i) {
        if (!envp || !envp[i]) return 0;
        size_t len = 0;
        while (envp[i][len]) ++len;
        size_t total_len = len + 1;
        if (string_cur < total_len) return 0;
        string_cur -= total_len;
        for (size_t c = 0; c <= len; ++c) {
            stack_mem[string_cur + c] = (uint8_t)envp[i][c];
        }
        user_env_ptrs[i] = USER_STACK_PAGE + string_cur;
    }
    user_env_ptrs[envc] = 0;

    struct process_auxiliary_pair auxiliary_pairs[16];
    size_t auxiliary_count = process_build_auxiliary_vector(
        process, auxiliary, user_execfn, auxiliary_pairs,
        sizeof(auxiliary_pairs) / sizeof(auxiliary_pairs[0]));
    if (auxiliary_count == 0) return 0;
    size_t table_slots = 1 + (size_t)(argc + 1) +
                         (size_t)(envc + 1) + auxiliary_count * 2;
    size_t table_bytes = table_slots * sizeof(uint64_t);
    if (string_cur < table_bytes) return 0;

    size_t table_offset = (string_cur - table_bytes) & ~(size_t)15;
    uint64_t *table = (uint64_t *)(uintptr_t)(stack_mem + table_offset);

    table[0] = (uint64_t)argc;
    for (int i = 0; i <= argc; ++i) {
        table[1 + i] = user_argv_ptrs[i];
    }
    for (int i = 0; i <= envc; ++i) {
        table[1 + argc + 1 + i] = user_env_ptrs[i];
    }
    size_t aux_offset = 1 + (size_t)argc + 1 + (size_t)envc + 1;
    for (size_t index = 0; index < auxiliary_count; ++index) {
        table[aux_offset + index * 2] = auxiliary_pairs[index].type;
        table[aux_offset + index * 2 + 1] = auxiliary_pairs[index].value;
    }

    process->stack_top = USER_STACK_PAGE + table_offset;
    return 1;
#endif
}

static int dynamic_tag_supported(int64_t tag) {
    switch (tag) {
    case ELF_DT_NULL:
    /* These entries describe linker tables/flags. They require no runtime
     * action for a static PIE with relative relocations and no DT_NEEDED or
     * PLT relocations. Keep rejecting tags that imply symbol/dependency
     * resolution until the userspace dynamic loader owns that work. */
    case ELF_DT_HASH:
    case ELF_DT_STRTAB:
    case ELF_DT_SYMTAB:
    case ELF_DT_STRSZ:
    case ELF_DT_SYMENT:
    case ELF_DT_DEBUG:
    case ELF_DT_FLAGS:
    case ELF_DT_RELCOUNT:
    case ELF_DT_RELACOUNT:
    case ELF_DT_FLAGS_1:
    case ELF_DT_GNU_HASH:
    case ELF_DT_RELA:
    case ELF_DT_RELASZ:
    case ELF_DT_RELAENT:
    case ELF_DT_REL:
    case ELF_DT_RELSZ:
    case ELF_DT_RELENT:
        return 1;
    /* A DT_NEEDED/PLT/symbol-table entry would require resolving another
     * object. Reject it until the shared-object namespace is available. */
    default:
        return 0;
    }
}

static int parse_dynamic_segment32(const uint8_t *image, size_t image_size,
                                   const struct elf32_program_header *segment,
                                   struct process_dynamic_metadata *metadata) {
    if (!image || !segment || !metadata || segment->filesz == 0 ||
        segment->filesz % sizeof(struct elf32_dynamic) != 0 ||
        !range_inside(segment->offset, segment->filesz, image_size)) return 0;
    metadata->enabled = 1;
    const uint8_t *entry_bytes = image + segment->offset;
    size_t count = segment->filesz / sizeof(struct elf32_dynamic);
    int terminated = 0;
    for (size_t index = 0; index < count; ++index) {
        struct elf32_dynamic entry;
        xscore_memcpy(&entry,
                      entry_bytes + index * sizeof(struct elf32_dynamic),
                      sizeof(entry));
        int64_t tag = entry.tag;
        uint64_t value = entry.value;
        if (!dynamic_tag_supported(tag)) return 0;
        if (tag == ELF_DT_NULL) { terminated = 1; break; }
        if (tag == ELF_DT_RELA) metadata->rela_vaddr = value;
        else if (tag == ELF_DT_RELASZ) metadata->rela_size = value;
        else if (tag == ELF_DT_RELAENT) metadata->rela_ent = value;
        else if (tag == ELF_DT_REL) metadata->rel_vaddr = value;
        else if (tag == ELF_DT_RELSZ) metadata->rel_size = value;
        else if (tag == ELF_DT_RELENT) metadata->rel_ent = value;
    }
    if (!terminated) return 0;
    if (metadata->rela_size != 0 &&
        (metadata->rela_vaddr == 0 || metadata->rela_ent != sizeof(struct elf32_rela) ||
         metadata->rela_size % metadata->rela_ent != 0)) return 0;
    if (metadata->rel_size != 0 &&
        (metadata->rel_vaddr == 0 || metadata->rel_ent != sizeof(struct elf32_rel) ||
         metadata->rel_size % metadata->rel_ent != 0)) return 0;
    return 1;
}

static int parse_dynamic_segment64(const uint8_t *image, size_t image_size,
                                   const struct elf64_program_header *segment,
                                   struct process_dynamic_metadata *metadata) {
    if (!image || !segment || !metadata || segment->filesz == 0 ||
        segment->filesz % sizeof(struct elf64_dynamic) != 0 ||
        !range_inside(segment->offset, segment->filesz, image_size)) return 0;
    metadata->enabled = 1;
    const uint8_t *entry_bytes = image + segment->offset;
    size_t count = segment->filesz / sizeof(struct elf64_dynamic);
    int terminated = 0;
    for (size_t index = 0; index < count; ++index) {
        struct elf64_dynamic entry;
        xscore_memcpy(&entry,
                      entry_bytes + index * sizeof(struct elf64_dynamic),
                      sizeof(entry));
        int64_t tag = entry.tag;
        uint64_t value = entry.value;
        if (!dynamic_tag_supported(tag)) return 0;
        if (tag == ELF_DT_NULL) { terminated = 1; break; }
        if (tag == ELF_DT_RELA) metadata->rela_vaddr = value;
        else if (tag == ELF_DT_RELASZ) metadata->rela_size = value;
        else if (tag == ELF_DT_RELAENT) metadata->rela_ent = value;
        else if (tag == ELF_DT_REL) metadata->rel_vaddr = value;
        else if (tag == ELF_DT_RELSZ) metadata->rel_size = value;
        else if (tag == ELF_DT_RELENT) metadata->rel_ent = value;
    }
    if (!terminated) return 0;
    if (metadata->rela_size != 0 &&
        (metadata->rela_vaddr == 0 || metadata->rela_ent != sizeof(struct elf64_rela) ||
         metadata->rela_size % metadata->rela_ent != 0)) return 0;
    if (metadata->rel_size != 0 &&
        (metadata->rel_vaddr == 0 || metadata->rel_ent != sizeof(struct elf64_rel) ||
         metadata->rel_size % metadata->rel_ent != 0)) return 0;
    return 1;
}

static int image_vaddr_to_offset(const uint8_t *image, size_t image_size,
                                 uint64_t virtual_address, uint64_t length,
                                 uint64_t *file_offset) {
    if (!image || image_size < 16 || !file_offset) return 0;
    if (image[4] == 1) {
        if (image_size < sizeof(struct elf32_header)) return 0;
        const struct elf32_header *header = (const void *)image;
        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf32_program_header *segment = (const void *)(
                image + header->phoff + (uint64_t)index * header->phentsize);
            uint64_t start = segment->vaddr;
            uint64_t end = start + segment->filesz;
            if (segment->type == ELF_PT_LOAD && end >= start &&
                virtual_address >= start && virtual_address <= end &&
                length <= end - virtual_address) {
                uint64_t offset = segment->offset + (virtual_address - start);
                if (offset >= segment->offset && range_inside(offset, length, image_size)) {
                    *file_offset = offset;
                    return 1;
                }
            }
        }
    } else if (image[4] == 2) {
        if (image_size < sizeof(struct elf64_header)) return 0;
        const struct elf64_header *header = (const void *)image;
        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf64_program_header *segment = (const void *)(
                image + header->phoff + (uint64_t)index * header->phentsize);
            uint64_t start = segment->vaddr;
            uint64_t end = start + segment->filesz;
            if (segment->type == ELF_PT_LOAD && end >= start &&
                virtual_address >= start && virtual_address <= end &&
                length <= end - virtual_address) {
                uint64_t offset = segment->offset + (virtual_address - start);
                if (offset >= segment->offset && range_inside(offset, length, image_size)) {
                    *file_offset = offset;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int process_mark_relocation_page(
    struct process_dynamic_metadata *metadata, uint64_t target) {
    if (!metadata || target >= USER_IMAGE_LOAD_WINDOW) return 0;
    uint64_t page_index = target / PAGE_SIZE;
    if (page_index >= USER_DYNAMIC_RELOCATION_PAGE_COUNT) return 0;
    metadata->relocation_page_bitmap[page_index / 64U] |=
        UINT64_C(1) << (page_index % 64U);
    return 1;
}

static int process_build_relocation_page_bitmap(
    const uint8_t *image, size_t image_size,
    struct process_dynamic_metadata *metadata) {
    if (!metadata || !metadata->enabled) return 1;
    if (!image || image_size < 16) return 0;
    uint64_t file_offset = 0;

    if (image[4] == 1) {
        if (metadata->rela_size) {
            if (metadata->rela_ent != sizeof(struct elf32_rela) ||
                metadata->rela_size % sizeof(struct elf32_rela) != 0 ||
                !image_vaddr_to_offset(image, image_size,
                                       metadata->rela_vaddr,
                                       metadata->rela_size, &file_offset)) {
                return 0;
            }
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = metadata->rela_size / sizeof(struct elf32_rela);
            for (size_t index = 0; index < count; ++index) {
                struct elf32_rela entry;
                xscore_memcpy(&entry,
                              entry_bytes + index * sizeof(entry),
                              sizeof(entry));
                if (!process_mark_relocation_page(
                        metadata, entry.offset)) return 0;
            }
        }
        if (metadata->rel_size) {
            if (metadata->rel_ent != sizeof(struct elf32_rel) ||
                metadata->rel_size % sizeof(struct elf32_rel) != 0 ||
                !image_vaddr_to_offset(image, image_size,
                                       metadata->rel_vaddr,
                                       metadata->rel_size, &file_offset)) {
                return 0;
            }
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = metadata->rel_size / sizeof(struct elf32_rel);
            for (size_t index = 0; index < count; ++index) {
                struct elf32_rel entry;
                xscore_memcpy(&entry,
                              entry_bytes + index * sizeof(entry),
                              sizeof(entry));
                if (!process_mark_relocation_page(
                        metadata, entry.offset)) return 0;
            }
        }
    } else if (image[4] == 2) {
        if (metadata->rela_size) {
            if (metadata->rela_ent != sizeof(struct elf64_rela) ||
                metadata->rela_size % sizeof(struct elf64_rela) != 0 ||
                !image_vaddr_to_offset(image, image_size,
                                       metadata->rela_vaddr,
                                       metadata->rela_size, &file_offset)) {
                return 0;
            }
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = metadata->rela_size / sizeof(struct elf64_rela);
            for (size_t index = 0; index < count; ++index) {
                struct elf64_rela entry;
                xscore_memcpy(&entry,
                              entry_bytes + index * sizeof(entry),
                              sizeof(entry));
                if (!process_mark_relocation_page(
                        metadata, entry.offset)) return 0;
            }
        }
        if (metadata->rel_size) {
            if (metadata->rel_ent != sizeof(struct elf64_rel) ||
                metadata->rel_size % sizeof(struct elf64_rel) != 0 ||
                !image_vaddr_to_offset(image, image_size,
                                       metadata->rel_vaddr,
                                       metadata->rel_size, &file_offset)) {
                return 0;
            }
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = metadata->rel_size / sizeof(struct elf64_rel);
            for (size_t index = 0; index < count; ++index) {
                struct elf64_rel entry;
                xscore_memcpy(&entry,
                              entry_bytes + index * sizeof(entry),
                              sizeof(entry));
                if (!process_mark_relocation_page(
                        metadata, entry.offset)) return 0;
            }
        }
    } else {
        return 0;
    }
    return 1;
}

static int process_dynamic_page_has_relocations(
    const struct process_dynamic_metadata *metadata, uint64_t page_offset) {
    if (!metadata || !metadata->enabled) return 0;
    if ((page_offset & (PAGE_SIZE - 1U)) != 0 ||
        page_offset >= USER_IMAGE_LOAD_WINDOW) return -1;
    uint64_t page_index = page_offset / PAGE_SIZE;
    return (metadata->relocation_page_bitmap[page_index / 64U] &
            (UINT64_C(1) << (page_index % 64U))) != 0;
}

static int dynamic_relocation_is_relative(uint64_t info) {
    uint32_t type = (uint32_t)info;
#if defined(__x86_64__) || defined(__i386__)
    return type == 8U;
#elif defined(__aarch64__)
    return type == 1027U;
#elif defined(__arm__)
    return type == 23U;
#elif defined(__riscv)
    return type == 3U;
#else
    return 0;
#endif
}

static int process_apply_dynamic_relocations(struct xscore_process *process,
                                             uint8_t *destination,
                                             uint64_t page_offset) {
    struct process_image_backing *backing = process_image_backing(process);
    if (!backing || !backing->dynamic.enabled || !destination) return 1;
    int has_relocations = process_dynamic_page_has_relocations(
        &backing->dynamic, page_offset);
    if (has_relocations < 0) return 0;
    if (!has_relocations) return 1;
    const uint8_t *image = backing->image;
    size_t image_size = backing->size;
    uint64_t base = backing->load_base;
    if (image[4] == 1) {
        uint64_t file_offset = 0;
        if (backing->dynamic.rela_size) {
            if (!image_vaddr_to_offset(image, image_size, backing->dynamic.rela_vaddr,
                                       backing->dynamic.rela_size, &file_offset)) return 0;
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = backing->dynamic.rela_size /
                           sizeof(struct elf32_rela);
            for (size_t i = 0; i < count; ++i) {
                struct elf32_rela entry;
                xscore_memcpy(&entry, entry_bytes + i * sizeof(entry),
                              sizeof(entry));
                uint64_t target = entry.offset;
                if (align_down(target) != page_offset ||
                    !dynamic_relocation_is_relative(entry.info)) continue;
                uint64_t within = target - page_offset;
                if (within + sizeof(uint32_t) > PAGE_SIZE) return 0;
                *(uint32_t *)(void *)(destination + within) =
                    (uint32_t)(base + (int64_t)entry.addend);
            }
        }
        if (backing->dynamic.rel_size) {
            if (!image_vaddr_to_offset(image, image_size, backing->dynamic.rel_vaddr,
                                       backing->dynamic.rel_size, &file_offset)) return 0;
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = backing->dynamic.rel_size /
                           sizeof(struct elf32_rel);
            for (size_t i = 0; i < count; ++i) {
                struct elf32_rel entry;
                xscore_memcpy(&entry, entry_bytes + i * sizeof(entry),
                              sizeof(entry));
                uint64_t target = entry.offset;
                if (align_down(target) != page_offset ||
                    !dynamic_relocation_is_relative(entry.info)) continue;
                uint64_t within = target - page_offset;
                if (within + sizeof(uint32_t) > PAGE_SIZE) return 0;
                uint32_t value = *(uint32_t *)(void *)(destination + within);
                *(uint32_t *)(void *)(destination + within) = value + (uint32_t)base;
            }
        }
    } else if (image[4] == 2) {
        uint64_t file_offset = 0;
        if (backing->dynamic.rela_size) {
            if (!image_vaddr_to_offset(image, image_size, backing->dynamic.rela_vaddr,
                                       backing->dynamic.rela_size, &file_offset)) return 0;
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = backing->dynamic.rela_size /
                           sizeof(struct elf64_rela);
            for (size_t i = 0; i < count; ++i) {
                struct elf64_rela entry;
                xscore_memcpy(&entry, entry_bytes + i * sizeof(entry),
                              sizeof(entry));
                uint64_t target = entry.offset;
                if (align_down(target) != page_offset ||
                    !dynamic_relocation_is_relative(entry.info)) continue;
                uint64_t within = target - page_offset;
                if (within + sizeof(uint64_t) > PAGE_SIZE) return 0;
                *(uint64_t *)(void *)(destination + within) =
                    base + (int64_t)entry.addend;
            }
        }
        if (backing->dynamic.rel_size) {
            if (!image_vaddr_to_offset(image, image_size, backing->dynamic.rel_vaddr,
                                       backing->dynamic.rel_size, &file_offset)) return 0;
            const uint8_t *entry_bytes = image + file_offset;
            size_t count = backing->dynamic.rel_size /
                           sizeof(struct elf64_rel);
            for (size_t i = 0; i < count; ++i) {
                struct elf64_rel entry;
                xscore_memcpy(&entry, entry_bytes + i * sizeof(entry),
                              sizeof(entry));
                uint64_t target = entry.offset;
                if (align_down(target) != page_offset ||
                    !dynamic_relocation_is_relative(entry.info)) continue;
                uint64_t within = target - page_offset;
                if (within + sizeof(uint64_t) > PAGE_SIZE) return 0;
                uint64_t value = *(uint64_t *)(void *)(destination + within);
                *(uint64_t *)(void *)(destination + within) = value + base;
            }
        }
    }
    return 1;
}

/* Return the PT_INTERP path embedded in an executable.  The kernel only
 * consumes this bounded string to select the native interpreter image; the
 * interpreter itself reads and relocates the main ET_DYN object in userspace.
 * A return value of 1 means found, 0 means absent, and -1 means malformed. */
static int elf_interpreter_path(const uint8_t *image, size_t image_size,
                                char *path, size_t path_capacity) {
    if (!image || image_size < 16 || !path || path_capacity < 2 ||
        image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' ||
        image[3] != 'F') return -1;
    path[0] = '\0';
    if (image[4] == 1) {
        if (image_size < sizeof(struct elf32_header)) return -1;
        const struct elf32_header *header = (const void *)image;
        if (header->phentsize != sizeof(struct elf32_program_header) ||
            !range_inside(header->phoff,
                          (uint64_t)header->phnum * header->phentsize,
                          image_size)) return -1;
        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf32_program_header *segment =
                (const void *)(image + header->phoff +
                               (uint64_t)index * header->phentsize);
            if (segment->type != ELF_PT_INTERP) continue;
            if (segment->filesz == 0 || segment->filesz > path_capacity ||
                !range_inside(segment->offset, segment->filesz, image_size)) {
                return -1;
            }
            size_t length = 0;
            while (length < segment->filesz && image[segment->offset + length]) {
                ++length;
            }
            if (length == segment->filesz || length + 1 >= path_capacity ||
                image[segment->offset] != '/') return -1;
            for (size_t c = 0; c <= length; ++c) {
                path[c] = (char)image[segment->offset + c];
            }
            return 1;
        }
        return 0;
    }
    if (image[4] == 2) {
        if (image_size < sizeof(struct elf64_header)) return -1;
        const struct elf64_header *header = (const void *)image;
        if (header->phentsize != sizeof(struct elf64_program_header) ||
            !range_inside(header->phoff,
                          (uint64_t)header->phnum * header->phentsize,
                          image_size)) return -1;
        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf64_program_header *segment =
                (const void *)(image + header->phoff +
                               (uint64_t)index * header->phentsize);
            if (segment->type != ELF_PT_INTERP) continue;
            if (segment->filesz == 0 || segment->filesz > path_capacity ||
                !range_inside(segment->offset, segment->filesz, image_size)) {
                return -1;
            }
            size_t length = 0;
            while (length < segment->filesz && image[segment->offset + length]) {
                ++length;
            }
            if (length == segment->filesz || length + 1 >= path_capacity ||
                image[segment->offset] != '/') return -1;
            for (size_t c = 0; c <= length; ++c) {
                path[c] = (char)image[segment->offset + c];
            }
            return 1;
        }
        return 0;
    }
    return -1;
}

static int load_elf_image(struct xscore_process *process, const uint8_t *image,
                          size_t image_size, int argc,
                          const char *const argv[], int envc,
                          const char *const envp[], const char *exec_path,
                          int is_interpreter) {
    if (!image || image_size < 16) return 0;
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') return 0;

    uint64_t entry_point = 0;
    uint64_t entry_page = 0;
    uint64_t load_base = USER_CODE_ADDRESS;
    int found_load = 0;
    int dynamic_image = 0;
    struct process_dynamic_metadata dynamic_metadata = {0};
    struct process_auxiliary_metadata auxiliary_metadata = {0};

    if (image[4] == 1) {
        /* ELFCLASS32: x86_32 (0x03), ARM32 (0x28), RISC-V32 (0xF3) */
        if (image_size < sizeof(struct elf32_header)) return 0;
        const struct elf32_header *header = (const void *)image;
        int valid_machine = (header->machine == 0x03 || header->machine == 0x28 || header->machine == 0xF3);
        if ((header->type != 2 && header->type != 3) || !valid_machine || header->version != 1 ||
            header->ehsize != sizeof(*header) ||
            header->phentsize != sizeof(struct elf32_program_header) ||
            !range_inside(header->phoff, (uint64_t)header->phnum * header->phentsize, image_size)) {
            return 0;
        }
        dynamic_image = header->type == 3;
        /* ET_EXEC images are linked at their runtime address. ET_DYN images
         * receive the native user base and may carry a static relocation table.
         * A main image declaring PT_INTERP is staged through the native
         * interpreter before this routine is called; DT_NEEDED remains a
         * userspace dlfcn responsibility. */
        if (!dynamic_image && (header->machine == 0x03 || header->machine == 0x28 ||
            header->machine == 0xF3)) load_base = 0;
        entry_point = header->entry;
        auxiliary_metadata.program_header_entry_size = header->phentsize;
        auxiliary_metadata.program_header_count = header->phnum;

        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf32_program_header *segment =
                (const void *)(image + header->phoff + (uint64_t)index * header->phentsize);
            uint64_t phdr_bytes = (uint64_t)header->phnum * header->phentsize;
            if (segment->type == ELF_PT_PHDR &&
                segment->vaddr <= UINT64_MAX - load_base) {
                auxiliary_metadata.program_header = load_base + segment->vaddr;
            } else if (segment->type == ELF_PT_LOAD &&
                       auxiliary_metadata.program_header == 0 &&
                       header->phoff >= segment->offset &&
                       header->phoff - segment->offset <= segment->filesz &&
                       phdr_bytes <= (uint64_t)segment->filesz -
                                     (header->phoff - segment->offset) &&
                       segment->vaddr <= UINT64_MAX - load_base &&
                       load_base + segment->vaddr <= UINT64_MAX -
                           (header->phoff - segment->offset)) {
                auxiliary_metadata.program_header = load_base + segment->vaddr +
                    (header->phoff - segment->offset);
            }
            if (segment->type == ELF_PT_INTERP) return 0;
            if (segment->type == ELF_PT_DYNAMIC) {
                if (!dynamic_image || !parse_dynamic_segment32(
                        image, image_size, segment, &dynamic_metadata)) return 0;
                continue;
            }
            if (segment->type != ELF_PT_LOAD || segment->memsz == 0) continue;
            uint64_t image_limit = load_base ? USER_IMAGE_LOAD_WINDOW :
                (USER_CODE_ADDRESS + USER_IMAGE_LOAD_WINDOW);
            if (segment->filesz > segment->memsz ||
                !range_inside(segment->offset, segment->filesz, image_size) ||
                segment->vaddr + segment->memsz > image_limit) return 0;

            uint64_t segment_start = align_down(segment->vaddr);
            uint64_t segment_end = (segment->vaddr + segment->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t signal_start = load_base ? USER_SIGNAL_TRAMPOLINE_OFFSET :
                                                USER_SIGNAL_TRAMPOLINE_ADDRESS;
            if (signal_start >= segment_start && signal_start < segment_end) return 0;

            uint64_t flags = 0;
            if (segment->flags & ELF_PF_W) flags |= XSCORE_PAGE_WRITABLE;
            if (!(segment->flags & ELF_PF_X)) flags |= XSCORE_PAGE_NO_EXECUTE;

            for (uint64_t offset = segment_start; offset < segment_end; offset += PAGE_SIZE) {
                if (process->mapped_page_count >= USER_IMAGE_MAX_PAGES) return 0;
                if (!process_reserve_image_page(process, load_base + offset,
                                                flags)) return 0;
                if (header->entry >= offset && header->entry < offset + PAGE_SIZE) {
                    entry_page = load_base + offset;
                }
            }
            found_load = 1;
        }
    } else if (image[4] == 2) {
        /* ELFCLASS64: x86_64 (0x3E), AArch64 (0xB7), RISC-V64 (0xF3) */
        if (image_size < sizeof(struct elf64_header)) return 0;
        const struct elf64_header *header = (const void *)image;
        int valid_machine = (header->machine == 0x3E || header->machine == 0xB7 || header->machine == 0xF3);
        if ((header->type != 2 && header->type != 3) || !valid_machine || header->version != 1 ||
            header->ehsize != sizeof(*header) ||
            header->phentsize != sizeof(struct elf64_program_header) ||
            !range_inside(header->phoff, (uint64_t)header->phnum * header->phentsize, image_size)) {
            return 0;
        }
        dynamic_image = header->type == 3;
        if (!dynamic_image && (header->machine == 0xB7 || header->machine == 0xF3)) {
            load_base = 0;
        }
        entry_point = header->entry;
        auxiliary_metadata.program_header_entry_size = header->phentsize;
        auxiliary_metadata.program_header_count = header->phnum;

        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf64_program_header *segment =
                (const void *)(image + header->phoff + (uint64_t)index * header->phentsize);
            uint64_t phdr_bytes = (uint64_t)header->phnum * header->phentsize;
            if (segment->type == ELF_PT_PHDR &&
                segment->vaddr <= UINT64_MAX - load_base) {
                auxiliary_metadata.program_header = load_base + segment->vaddr;
            } else if (segment->type == ELF_PT_LOAD &&
                       auxiliary_metadata.program_header == 0 &&
                       header->phoff >= segment->offset &&
                       header->phoff - segment->offset <= segment->filesz &&
                       phdr_bytes <= segment->filesz -
                                     (header->phoff - segment->offset) &&
                       segment->vaddr <= UINT64_MAX - load_base &&
                       load_base + segment->vaddr <= UINT64_MAX -
                           (header->phoff - segment->offset)) {
                auxiliary_metadata.program_header = load_base + segment->vaddr +
                    (header->phoff - segment->offset);
            }
            if (segment->type == ELF_PT_INTERP) return 0;
            if (segment->type == ELF_PT_DYNAMIC) {
                if (!dynamic_image || !parse_dynamic_segment64(
                        image, image_size, segment, &dynamic_metadata)) return 0;
                continue;
            }
            if (segment->type != ELF_PT_LOAD || segment->memsz == 0) continue;
            uint64_t image_limit = load_base ? USER_IMAGE_LOAD_WINDOW :
                (USER_CODE_ADDRESS + USER_IMAGE_LOAD_WINDOW);
            if (segment->filesz > segment->memsz ||
                !range_inside(segment->offset, segment->filesz, image_size) ||
                segment->vaddr > UINT64_MAX - segment->memsz ||
                segment->vaddr + segment->memsz > image_limit) return 0;

            uint64_t segment_start = align_down(segment->vaddr);
            uint64_t segment_end = (segment->vaddr + segment->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t signal_start = load_base ? USER_SIGNAL_TRAMPOLINE_OFFSET :
                                                USER_SIGNAL_TRAMPOLINE_ADDRESS;
            if (signal_start >= segment_start && signal_start < segment_end) return 0;

            uint64_t flags = 0;
            if (segment->flags & ELF_PF_W) flags |= XSCORE_PAGE_WRITABLE;
            if (!(segment->flags & ELF_PF_X)) flags |= XSCORE_PAGE_NO_EXECUTE;

            for (uint64_t offset = segment_start; offset < segment_end; offset += PAGE_SIZE) {
                if (process->mapped_page_count >= USER_IMAGE_MAX_PAGES) return 0;
                if (!process_reserve_image_page(process, load_base + offset,
                                                flags)) return 0;
                if (header->entry >= offset && header->entry < offset + PAGE_SIZE) {
                    entry_page = load_base + offset;
                }
            }
            found_load = 1;
        }
    } else {
        return 0;
    }

    /* Linker scripts for the small native interpreter are intentionally
     * compact and may leave the ELF header/program-header table outside every
     * PT_LOAD segment.  AT_PHDR is still a required startup datum for a
     * PT_INTERP handoff; derive its canonical load-relative address from the
     * validated file header when no PT_PHDR/embedded-header segment supplied
     * one.  The userspace loader uses the value for validation and reads the
     * original image through VFS, so this does not force an extra executable
     * mapping into the normal page-cache layout. */
    if (auxiliary_metadata.program_header == 0) {
        uint64_t phoff = 0;
        uint64_t phentsize = 0;
        uint64_t phnum = 0;
        if (image[4] == 1 && image_size >= sizeof(struct elf32_header)) {
            const struct elf32_header *aux_header = (const void *)image;
            phoff = aux_header->phoff;
            phentsize = aux_header->phentsize;
            phnum = aux_header->phnum;
        } else if (image[4] == 2 && image_size >= sizeof(struct elf64_header)) {
            const struct elf64_header *aux_header = (const void *)image;
            phoff = aux_header->phoff;
            phentsize = aux_header->phentsize;
            phnum = aux_header->phnum;
        }
        uint64_t phdr_bytes = phentsize * phnum;
        if (phoff != 0 && phentsize != 0 && phnum != 0 &&
            phdr_bytes / phentsize == phnum &&
            range_inside(phoff, phdr_bytes, image_size) &&
            load_base <= UINT64_MAX - phoff) {
            auxiliary_metadata.program_header = load_base + phoff;
        }
    }

    if (!found_load || entry_page == 0 ||
        !process_build_relocation_page_bitmap(
            image, image_size, &dynamic_metadata)) return 0;
    if (load_base > UINT64_MAX - entry_point) return 0;
    auxiliary_metadata.entry = load_base + entry_point;
    auxiliary_metadata.interpreter_base = is_interpreter ? load_base : 0;
    auxiliary_metadata.interpreter_present = is_interpreter ? 1U : 0U;
    uint64_t stack_physical = 0;
    if (!process_map_new_page(process, USER_STACK_PAGE,
                              XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE,
                              &stack_physical)) return 0;
    if (!setup_user_stack_arguments(process, stack_physical, argc, argv,
                                    envc, envp, exec_path,
                                    &auxiliary_metadata)) return 0;

    uint64_t trampoline_physical = 0;
    if (!process_map_new_page(process, USER_SIGNAL_TRAMPOLINE_ADDRESS, 0,
                              &trampoline_physical)) return 0;
    uint8_t *trampoline = (uint8_t *)(uintptr_t)trampoline_physical;
    for (size_t i = 0; i < sizeof(signal_exit_trampoline); ++i) {
        trampoline[i] = signal_exit_trampoline[i];
    }
    process->signal_trampoline = USER_SIGNAL_TRAMPOLINE_ADDRESS;
    process->entry = load_base + entry_point;
    process->stack_bottom = USER_STACK_PAGE;
    struct process_image_backing *backing = process_image_backing(process);
    if (!backing) return 0;
    backing->image = image;
    backing->size = image_size;
    backing->load_base = load_base;
    process_dynamic_metadata_copy(&backing->dynamic, &dynamic_metadata);
    return 1;
}

static void process_sync_instruction_cache(void) {
#if defined(__aarch64__)
    __asm__ volatile("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
#elif defined(__arm__)
    uint32_t zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0; dsb sy; isb"
                     : : "r"(zero) : "memory");
#elif defined(__riscv)
    __asm__ volatile("fence.i" ::: "memory");
#endif
}

static void process_copy_file_range(uint8_t *destination,
                                    const uint8_t *image,
                                    uint64_t page_offset,
                                    uint64_t segment_vaddr,
                                    uint64_t segment_offset,
                                    uint64_t segment_filesz) {
    uint64_t copy_start = segment_vaddr > page_offset ?
                          segment_vaddr : page_offset;
    uint64_t copy_end = segment_vaddr + segment_filesz;
    if (copy_end > page_offset + PAGE_SIZE) copy_end = page_offset + PAGE_SIZE;
    if (copy_end <= copy_start) return;
    const uint8_t *source = image + segment_offset +
                            (copy_start - segment_vaddr);
    destination += copy_start - page_offset;
    for (uint64_t byte = 0; byte < copy_end - copy_start; ++byte) {
        destination[byte] = source[byte];
    }
}

static int process_load_image_page(struct xscore_process *process,
                                   uint32_t page_index) {
    struct process_image_backing *backing = process_image_backing(process);
    if (!backing || !backing->image ||
        page_index >= process->mapped_page_count ||
        process->physical_pages[page_index]) return 0;

    const uint8_t *image = backing->image;
    uint64_t page_offset = process->mapped_pages[page_index] -
                           backing->load_base;
    uint64_t flags = process->mapped_page_flags[page_index];
    int has_relocations = process_dynamic_page_has_relocations(
        &backing->dynamic, page_offset);
    if (has_relocations < 0) return 0;
    /* Immutable PIE pages without fixups are identical across processes. */
    int is_readonly = !(flags & XSCORE_PAGE_WRITABLE) && !has_relocations;

    (void)is_readonly;

    uint64_t physical = xscore_pmm_allocate_page();
    if (!physical) return 0;
    zero_page(physical);
    uint8_t *destination = (uint8_t *)(uintptr_t)physical;

    if (image[4] == 1) {
        const struct elf32_header *header = (const void *)image;
        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf32_program_header *segment = (const void *)(
                image + header->phoff + (uint64_t)index * header->phentsize);
            if (segment->type != ELF_PT_LOAD || segment->filesz == 0) continue;
            process_copy_file_range(destination, image, page_offset,
                                    segment->vaddr, segment->offset,
                                    segment->filesz);
        }
    } else if (image[4] == 2) {
        const struct elf64_header *header = (const void *)image;
        for (uint16_t index = 0; index < header->phnum; ++index) {
            const struct elf64_program_header *segment = (const void *)(
                image + header->phoff + (uint64_t)index * header->phentsize);
            if (segment->type != ELF_PT_LOAD || segment->filesz == 0) continue;
            process_copy_file_range(destination, image, page_offset,
                                    segment->vaddr, segment->offset,
                                    segment->filesz);
        }
    } else {
        xscore_pmm_free_page(physical);
        return 0;
    }

    if (!process_apply_dynamic_relocations(process, destination, page_offset)) {
        xscore_pmm_free_page(physical);
        return 0;
    }
    process_sync_instruction_cache();
    if (!xscore_vmm_map_page_in(process->address_space_root,
                                process->mapped_pages[page_index], physical,
                                flags)) {
        xscore_pmm_free_page(physical);
        return 0;
    }
    process->physical_pages[page_index] = physical;
    /* Pure direct demand loading avoids cross-process physical page aliasing */
    if (!(process_runtime_flags & PROCESS_RUNTIME_FILE_REPORTED)) {
        xscore_serial_write("[INFO] File-backed ELF page loaded on demand\n");
        process_runtime_flags |= PROCESS_RUNTIME_FILE_REPORTED;
    }
    return 1;
}

static int process_remap_page(struct xscore_process *process,
                              uint64_t virtual_address,
                              uint64_t physical_address,
                              uint64_t flags) {
    uint64_t old_physical = 0;
    if (!xscore_vmm_unmap_page_in(process->address_space_root,
                                  virtual_address, &old_physical)) return 0;
    if (xscore_vmm_map_page_in(process->address_space_root, virtual_address,
                               physical_address, flags)) return 1;
    (void)xscore_vmm_map_page_in(process->address_space_root, virtual_address,
                                 old_physical, flags);
    return 0;
}

static int process_resolve_cow(struct xscore_process *process,
                               uint64_t virtual_address,
                               uint64_t *physical_slot,
                               uint8_t *cow_slot,
                               uint64_t writable_flags) {
    if (!*cow_slot || !*physical_slot) return 0;
    uint64_t old_physical = *physical_slot;
    if (xscore_pmm_refcount_get(old_physical) == 1) {
        if (!process_remap_page(process, virtual_address, old_physical,
                                writable_flags | XSCORE_PAGE_WRITABLE)) return 0;
        *cow_slot = 0;
        if (!(process_runtime_flags & PROCESS_RUNTIME_COW_REPORTED)) {
            xscore_serial_write("[INFO] Copy-on-Write user fault resolved\n");
            process_runtime_flags |= PROCESS_RUNTIME_COW_REPORTED;
        }
        return 1;
    }

    uint64_t new_physical = xscore_pmm_allocate_page();
    if (!new_physical) return 0;
    uint64_t *source = (uint64_t *)(uintptr_t)old_physical;
    uint64_t *destination = (uint64_t *)(uintptr_t)new_physical;
    for (size_t word = 0; word < PAGE_SIZE / sizeof(uint64_t); ++word) {
        destination[word] = source[word];
    }
    if (!process_remap_page(process, virtual_address, new_physical,
                            writable_flags | XSCORE_PAGE_WRITABLE)) {
        xscore_pmm_free_page(new_physical);
        return 0;
    }
    (void)xscore_pmm_refcount_dec(old_physical);
    *physical_slot = new_physical;
    *cow_slot = 0;
    if (!(process_runtime_flags & PROCESS_RUNTIME_COW_REPORTED)) {
        xscore_serial_write("[INFO] Copy-on-Write user fault resolved\n");
        process_runtime_flags |= PROCESS_RUNTIME_COW_REPORTED;
    }
    return 1;
}

int xscore_process_handle_page_fault(uint64_t fault_address,
                                     uint32_t fault_flags) {
    struct xscore_process *process = xscore_process_current();
    if (!process || process->state != XSCORE_PROCESS_RUNNING ||
        process->stack_bottom == 0) return 0;

    uint64_t fault_page = align_down(fault_address);

    if (fault_flags & XSCORE_USER_FAULT_WRITE) {
        for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
            if (process->mapped_pages[index] == fault_page &&
                process->mapped_page_cow[index]) {
                return process_resolve_cow(
                    process, fault_page, &process->physical_pages[index],
                    &process->mapped_page_cow[index],
                    process->mapped_page_flags[index]);
            }
        }
        if (fault_page >= process->heap_start &&
            fault_page < process->heap_start +
                         (uint64_t)process->heap_page_count * PAGE_SIZE) {
            uint32_t heap_index =
                (uint32_t)((fault_page - process->heap_start) / PAGE_SIZE);
            if (process->heap_page_cow[heap_index]) {
                return process_resolve_cow(
                    process, fault_page,
                    &process->heap_physical_pages[heap_index],
                    &process->heap_page_cow[heap_index],
                    XSCORE_PAGE_USER | XSCORE_PAGE_NO_EXECUTE);
            }
        }
        for (uint32_t index = 0; index < process->mmap_page_count; ++index) {
            if (process->mmap_pages[index] == fault_page &&
                process->mmap_page_cow[index]) {
                if (!(process->mmap_protections[index] & AKAOS_PROT_WRITE)) {
                    return 0;
                }
                return process_resolve_cow(
                    process, fault_page,
                    &process->mmap_physical_pages[index],
                    &process->mmap_page_cow[index],
                    process->mmap_page_flags[index]);
            }
        }
    }

    if (!(fault_flags & XSCORE_USER_FAULT_PRESENT)) {
        if (fault_page >= USER_STACK_TOP - (uint64_t)USER_MAX_STACK_PAGES * PAGE_SIZE &&
            fault_page < USER_STACK_TOP) {
            for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
                if (process->mapped_pages[index] != fault_page) continue;
                if (process->physical_pages[index]) {
                    return xscore_vmm_map_page_in(
                        process->address_space_root, fault_page,
                        process->physical_pages[index],
                        process->mapped_page_flags[index]);
                }
                uint64_t physical = xscore_pmm_allocate_page();
                if (!physical) return 0;
                zero_page(physical);
                if (!xscore_vmm_map_page_in(
                        process->address_space_root, fault_page, physical,
                        process->mapped_page_flags[index])) {
                    xscore_pmm_free_page(physical);
                    return 0;
                }
                process->physical_pages[index] = physical;
                return 1;
            }
            /* Fresh unmapped stack page within valid user stack region */
            uint64_t physical = xscore_pmm_allocate_page();
            if (!physical) return 0;
            zero_page(physical);
            uint64_t flags = XSCORE_PAGE_PRESENT | XSCORE_PAGE_USER |
                             XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE;
            if (!xscore_vmm_map_page_in(process->address_space_root, fault_page,
                                        physical, flags)) {
                xscore_pmm_free_page(physical);
                return 0;
            }
            if (!process_track_page(process, fault_page, physical, flags)) {
                xscore_vmm_unmap_page_in(process->address_space_root, fault_page, 0);
                xscore_pmm_free_page(physical);
                return 0;
            }
            if (fault_page < process->stack_bottom) {
                process->stack_bottom = fault_page;
            }
            return 1;
        }

        for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
            if (process->mapped_pages[index] != fault_page) continue;
            if (process->physical_pages[index]) {
                /* Metadata can remain resident while a context switch leaves
                 * the current address space without its PTE. Restore the
                 * known page instead of treating it as an invalid fault. */
                return xscore_vmm_map_page_in(
                    process->address_space_root, fault_page,
                    process->physical_pages[index],
                    process->mapped_page_flags[index]);
            }
            if ((fault_flags & XSCORE_USER_FAULT_WRITE) &&
                !(process->mapped_page_flags[index] & XSCORE_PAGE_WRITABLE)) {
                return 0;
            }
            return process_load_image_page(process, index);
        }
    }

    if (!(fault_flags & XSCORE_USER_FAULT_PRESENT)) {
        for (uint32_t index = 0; index < process->mmap_page_count; ++index) {
            if (process->mmap_pages[index] != fault_page) continue;
            if (process->mmap_physical_pages[index]) {
                return xscore_vmm_map_page_in(
                    process->address_space_root, fault_page,
                    process->mmap_physical_pages[index],
                    process->mmap_page_flags[index]);
            }
            uint8_t protection = process->mmap_protections[index];
            if (protection == AKAOS_PROT_NONE ||
                ((fault_flags & XSCORE_USER_FAULT_WRITE) &&
                 !(protection & AKAOS_PROT_WRITE))) return 0;

            struct process_mmap_backing *backing =
                process->mmap_backings[index];
            uint64_t offset = process->mmap_backing_offsets[index];
            uint64_t physical = 0;
            int allocated = 0;
            int installed_shared_page = 0;
            int shared = backing &&
                         (backing->flags & MMAP_BACKING_SHARED) != 0;
            uint32_t backing_page = (uint32_t)(offset / PAGE_SIZE);
            if (shared && backing_page < USER_MAX_MMAP_PAGES) {
                physical = backing->physical_pages[backing_page];
            }
            if (!physical) {
                physical = xscore_pmm_allocate_page();
                if (!physical) return 0;
                allocated = 1;
                zero_page(physical);
            }
            if (backing && !shared) {
                uint8_t *destination = (uint8_t *)(uintptr_t)physical;
                uint64_t available = offset < backing->length ?
                    backing->length - offset : 0;
                if (available > PAGE_SIZE) available = PAGE_SIZE;
                for (uint64_t byte = 0; byte < available; ++byte)
                    destination[byte] = backing->data[offset + byte];
            }
            if (shared && backing_page < USER_MAX_MMAP_PAGES &&
                !backing->physical_pages[backing_page]) {
                backing->physical_pages[backing_page] = physical;
                installed_shared_page = 1;
            }
            if (protection & AKAOS_PROT_EXEC) process_sync_instruction_cache();
            if (!xscore_vmm_map_page_in(process->address_space_root,
                                        fault_page, physical,
                                        process->mmap_page_flags[index])) {
                if (installed_shared_page) {
                    backing->physical_pages[backing_page] = 0;
                }
                if (allocated) (void)xscore_pmm_refcount_dec(physical);
                return 0;
            }
            if (shared) xscore_pmm_refcount_inc(physical);
            process->mmap_physical_pages[index] = physical;
            return 1;
        }
    }

    /* brk() reserves an anonymous range. Commit a zero-filled page only on
     * first access, so untouched heap capacity consumes no physical memory. */
    if (!(fault_flags & XSCORE_USER_FAULT_PRESENT) &&
        fault_address >= process->heap_start &&
        fault_address < process->heap_break) {
        uint32_t heap_index =
            (uint32_t)((fault_page - process->heap_start) / PAGE_SIZE);
        if (heap_index >= process->heap_page_count ||
            process->heap_physical_pages[heap_index]) return 0;
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) return 0;
        zero_page(physical);
        if (!xscore_vmm_map_page_in(
                process->address_space_root, fault_page, physical,
                XSCORE_PAGE_USER | XSCORE_PAGE_WRITABLE |
                    XSCORE_PAGE_NO_EXECUTE)) {
            xscore_pmm_free_page(physical);
            return 0;
        }
        process->heap_physical_pages[heap_index] = physical;
        process->heap_page_cow[heap_index] = 0;
        if (!(process_runtime_flags & PROCESS_RUNTIME_ANON_REPORTED)) {
            xscore_serial_write("[INFO] Anonymous heap page committed on demand\n");
            process_runtime_flags |= PROCESS_RUNTIME_ANON_REPORTED;
        }
        return 1;
    }

    uint64_t stack_limit = USER_STACK_TOP -
                           (uint64_t)USER_MAX_STACK_PAGES * PAGE_SIZE;

    /* Grow contiguously down to fault_page. This maintains a contiguous mapping
     * within the reserved stack range while supporting stack frame allocations
     * larger than a single page. */
    if (fault_page >= stack_limit && fault_page < process->stack_bottom &&
        (process->stack_bottom - fault_page) <= (uint64_t)USER_MAX_STACK_PAGES * PAGE_SIZE) {
        while (process->stack_bottom > fault_page) {
            uint64_t next_page = process->stack_bottom - PAGE_SIZE;
            if (!process_map_new_page(process, next_page,
                                      XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE,
                                      0)) return 0;
            process->stack_bottom = next_page;
        }
        return 1;
    }
    return 0;
}

/* Publish process death and orphan adoption as one process-table transition.
 * The table lock serializes this with reaper replacement and waitpid's child
 * scan; per-process locks keep getppid() from observing a torn parent PID. */
static void process_mark_exited(struct xscore_process *process,
                                uint64_t exit_status) {
    if (!process) return;
    uintptr_t table_flags = xscore_spin_lock_irqsave(&process_table_lock);
    uint64_t pid = process->pid;
    uint64_t adopter_pid = process_reaper_pid;
    if (adopter_pid == pid) {
        process_reaper_pid = 0;
        adopter_pid = 0;
    }

    if (adopter_pid) {
        int reaper_is_live = 0;
        for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
            struct xscore_process *candidate = &processes[index];
            if (candidate->pid != adopter_pid) continue;
            uintptr_t candidate_flags =
                xscore_spin_lock_irqsave(&candidate->lock);
            enum xscore_process_state state = candidate->state;
            reaper_is_live = state == XSCORE_PROCESS_READY ||
                             state == XSCORE_PROCESS_RUNNING ||
                             state == XSCORE_PROCESS_STOPPED;
            xscore_spin_unlock_irqrestore(&candidate->lock, candidate_flags);
            break;
        }
        if (!reaper_is_live) adopter_pid = 0;
    }

    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        struct xscore_process *child = &processes[index];
        if (child == process) continue;
        uintptr_t child_flags = xscore_spin_lock_irqsave(&child->lock);
        if (process_state_is_visible(child->state) &&
            child->parent_pid == pid) {
            child->parent_pid = adopter_pid;
        }
        xscore_spin_unlock_irqrestore(&child->lock, child_flags);
    }

    uintptr_t process_flags = xscore_spin_lock_irqsave(&process->lock);
    uint64_t waiter_thread_id = 0;
    uint64_t parent_pid = 0;
    uint32_t child_uid = 0;
    int newly_exited = 0;
    if (process->state != XSCORE_PROCESS_REAPED &&
        process->state != XSCORE_PROCESS_UNUSED) {
        newly_exited = process->state != XSCORE_PROCESS_EXITED;
        process->exit_status = exit_status;
        process->state = XSCORE_PROCESS_EXITED;
        waiter_thread_id = process->waiter_thread_id;
        parent_pid = process->parent_pid;
        child_uid = process->effective_uid;
    }
    xscore_spin_unlock_irqrestore(&process->lock, process_flags);

    uint64_t parent_thread_id = 0;
    if (newly_exited && parent_pid != 0) {
        for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
            struct xscore_process *parent = &processes[index];
            if (parent == process || parent->pid != parent_pid) continue;

            uintptr_t parent_flags = xscore_spin_lock_irqsave(&parent->lock);
            enum xscore_process_state parent_state = parent->state;
            uint32_t sigchld_bit = UINT32_C(1) << AKAOS_SIGCHLD;
            if ((parent_state == XSCORE_PROCESS_READY ||
                 parent_state == XSCORE_PROCESS_RUNNING ||
                 parent_state == XSCORE_PROCESS_STOPPED) &&
                !(parent->ignored_signals & sigchld_bit) &&
                parent->signal_actions[AKAOS_SIGCHLD].sa_handler !=
                    AKAOS_SIG_IGN) {
                /* Standard signals coalesce. Preserve an unrelated pending
                 * signal rather than replacing it with SIGCHLD. */
                if (parent->pending_signal == 0) {
                    parent->pending_signal = AKAOS_SIGCHLD;
                    parent->pending_signal_code = AKAOS_SI_KERNEL;
                    parent->pending_signal_address = 0;
                    parent->pending_signal_sender_pid = pid;
                    parent->pending_signal_sender_uid = child_uid;
                }
                if (parent->pending_signal == AKAOS_SIGCHLD)
                    parent_thread_id = parent->thread_id;
            }
            xscore_spin_unlock_irqrestore(&parent->lock, parent_flags);
            break;
        }
    }
    xscore_spin_unlock_irqrestore(&process_table_lock, table_flags);
    if (waiter_thread_id) (void)xscore_thread_wake(waiter_thread_id);
    if (parent_thread_id) (void)xscore_thread_wake(parent_thread_id);
}

static void process_thread_main(void *argument) {
    struct xscore_process *process = argument;
    if (!process || xscore_vmm_current_root() != process->address_space_root) {
        if (process) {
            process_mark_exited(process, 127U);
        }
        xscore_serial_write("[FATAL] User task entered with the wrong address space\n");
        return;
    }
#if defined(__x86_64__) || defined(__i386__)
    if (xscore_tss_rsp0() != xscore_thread_current_privilege_stack_top()) {
        process_mark_exited(process, 127U);
        xscore_serial_write("[FATAL] User task entered without its TSS.RSP0 stack\n");
        return;
    }
#endif
    uintptr_t process_flags = xscore_spin_lock_irqsave(&process->lock);
    process->state = XSCORE_PROCESS_RUNNING;
    xscore_spin_unlock_irqrestore(&process->lock, process_flags);
    serial_process_status("[INFO] Entering Ring 3 process ", process->pid, "\n");
    xscore_syscall_prepare_process();
    if (process->fork_resume) {
        /* Obtain the transition first, then reload the context pointer.  The
         * AArch64 IRQ/scheduler path may switch and restore callee-saved
         * registers while this helper runs; keeping the pointer in a kernel
         * register across that call could otherwise turn it into NULL. */
        struct xscore_user_transition *transition =
            xscore_thread_current_user_transition();
        const void *fork_context = (const void *)process->fork_context;
        if (!fork_context) {
            serial_process_status("[FATAL] Missing fork context for process ",
                                  process->pid, "\n");
            xscore_thread_record_user_exit(UINT64_C(127));
            return;
        }
#if defined(__aarch64__)
        /* The resume helper consumes caller-saved argument registers before
         * it can establish its own protected frame.  Mask the timer IRQ for
         * this tiny handoff so a scheduler switch cannot replace x0/x1.  The
         * saved user SPSR restores the intended EL0 interrupt state on eret. */
        __asm__ volatile("msr daifset, #2" ::: "memory");
#endif
        xscore_user_resume(fork_context, transition);
    } else {
        struct xscore_user_transition *transition =
            xscore_thread_current_user_transition();
        uint64_t entry = process->entry;
        uint64_t stack = process->stack_top;
        xscore_user_enter(entry, stack, transition);
    }

    /* A user transition may return through an exception path that restores
     * only the architectural context, and older ARM firmware/QEMU versions
     * have been observed to leave a caller-saved kernel register altered.
     * Resolve the process from the current thread before touching process
     * state again instead of trusting the pre-entry pointer register. */
    struct xscore_process *returned_process = xscore_process_current();
    if (returned_process) process = returned_process;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sti");
#elif defined(__aarch64__)
    __asm__ volatile("msr daifclr, #2");
#elif defined(__arm__)
    __asm__ volatile("cpsie i");
#elif defined(__riscv)
    __asm__ volatile("csrsi sstatus, 2");
#endif
    process_mark_exited(process, xscore_syscall_last_exit_status());
    serial_process_status("[INFO] User process ", process->pid, " exited\n");
    /* The status tells a program that ended on purpose apart from one that
     * died, and a signal is reported as the status a shell would show. */
    serial_process_status("[INFO] User process ", process->pid, " exit status ");
    serial_process_status("", xscore_syscall_last_exit_status(), "\n");
}

int xscore_process_initialize(void) {
    if (process_runtime_flags & PROCESS_RUNTIME_INITIALIZED) return 1;
    xscore_page_cache_initialize();
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        xscore_spinlock_init(&processes[index].lock);
        processes[index].state = XSCORE_PROCESS_UNUSED;
        processes[index].image_node_id = 0;
        processes[index].image_node_ref = 0;
    }
    for (uint32_t index = 0; index < PROCESS_SHM_MAX_OBJECTS; ++index) {
        shm_objects[index].in_use = 0;
        shm_objects[index].backing = 0;
    }
    next_pid = 1;
    process_reaper_pid = 0;
    tty_foreground_pgid = 0;
    tty_shell_pgid = 0;
    process_runtime_flags = PROCESS_RUNTIME_INITIALIZED;
    return 1;
}

static void set_process_name_from_path(struct xscore_process *process, const char *path) {
    if (!process) return;
    if (!path || !*path) {
        process->name[0] = '\0';
        return;
    }
    const char *last_slash = 0;
    for (const char *p = path; *p; ++p) {
        if (*p == '/') last_slash = p;
    }
    const char *name = last_slash ? last_slash + 1 : path;
    size_t i = 0;
    while (name[i] && i < sizeof(process->name) - 1) {
        process->name[i] = name[i];
        ++i;
    }
    process->name[i] = '\0';
}

static int process_apply_spawn_actions(
    struct xscore_process *process,
    const struct akaos_spawn_file_action *actions, uint32_t action_count) {
    if (!process || action_count > 8U || (action_count != 0 && !actions)) {
        return 0;
    }
    for (uint32_t index = 0; index < action_count; ++index) {
        const struct akaos_spawn_file_action *action = &actions[index];
        int64_t result;
        if (action->type == AKAOS_SPAWN_ACTION_CLOSE) {
            result = xscore_fd_close(&process->file_descriptors,
                                     action->first);
        } else if (action->type == AKAOS_SPAWN_ACTION_CLOSEFROM) {
            result = xscore_fd_table_close_range(&process->file_descriptors,
                                                 action->first);
        } else if (action->type == AKAOS_SPAWN_ACTION_DUP2) {
            result = xscore_fd_dup2(&process->file_descriptors,
                                    action->first, action->second);
        } else {
            return 0;
        }
        if (result < 0) return 0;
    }
    return 1;
}

static struct xscore_process *process_create_elf_args_on_cpu(
    const uint8_t *image, size_t image_size, int argc,
    const char *const argv[], int envc, const char *const envp[],
    const char *exec_path,
    const struct akaos_spawn_file_action *actions, uint32_t action_count,
    uint32_t target_cpu, uint32_t image_node) {
    if (!(process_runtime_flags & PROCESS_RUNTIME_INITIALIZED) ||
        !image || image_size == 0) return 0;
    struct xscore_process *process = process_slot_reserve();
    if (!process) return 0;

    const uint8_t *load_image = image;
    size_t load_image_size = image_size;
    uint32_t load_image_node = image_node;
    char interpreter_path[ELF_INTERPRETER_PATH_MAX];
    int interpreter_result = elf_interpreter_path(
        image, image_size, interpreter_path, sizeof(interpreter_path));
    if (interpreter_result < 0) {
        if (image_node < XSCORE_VFS_MAX_NODES) {
            xscore_vfs_node_release(image_node);
        }
        process_slot_release(process);
        return 0;
    }
    if (interpreter_result > 0) {
        if (!xscore_vfs_lookup_file_ref_at(0, interpreter_path,
                                           &load_image, &load_image_size,
                                           &load_image_node)) {
            if (image_node < XSCORE_VFS_MAX_NODES) {
                xscore_vfs_node_release(image_node);
            }
            process_slot_release(process);
            return 0;
        }
        if (image_node < XSCORE_VFS_MAX_NODES && load_image_node != image_node) {
            xscore_vfs_node_release(image_node);
        }
    }
    if (load_image_node < XSCORE_VFS_MAX_NODES) {
        process->image_node_id = load_image_node;
        process->image_node_ref = 1;
    }

    process->thread_id = 0;
    process->waiter_thread_id = 0;
    process->entry = 0;
    process->stack_top = 0;
    process->stack_bottom = 0;
    process->mapped_page_count = 0;
    process->heap_start = USER_HEAP_START;
    process->heap_break = USER_HEAP_START;
    process->heap_page_count = 0;
    process->mmap_page_count = 0;
    process->mmap_next_address = USER_MMAP_START;
    process->fork_context_size = 0;
    process->fork_resume = 0;
    if (!process_context_allocate(process)) {
        process_image_release(process);
        process_slot_release(process);
        return 0;
    }
    struct process_image_backing *new_backing = process_image_backing(process);
    new_backing->image = 0;
    new_backing->size = 0;
    new_backing->load_base = 0;
    process->exit_status = UINT64_MAX;
    process->termination_signal = 0U;
    process->wait_blocked = 0;
    process->parent_pid = 0;
    process->pending_signal = 0;
    process->pending_signal_sender_pid = 0;
    process->pending_signal_sender_uid = 0;
    /* Executed programs start with default dispositions. Interactive shells
     * explicitly ignore SIGINT after startup. */
    process->ignored_signals = 0;
    process->signal_mask = 0;
    for (uint32_t s = 0; s <= AKAOS_SIGNAL_MAX; ++s) {
        process->signal_actions[s].sa_handler = AKAOS_SIG_DFL;
        process->signal_actions[s].sa_mask = 0;
        process->signal_actions[s].sa_flags = 0;
        process->signal_actions[s].sa_restorer = 0;
    }
    process->foreground = 0;
    process->signal_trampoline = 0;
    struct xscore_process *parent = xscore_process_current();
    if (parent) {
        process->parent_pid = parent->pid;
        process->pgid = parent->pgid ? parent->pgid : parent->pid;
        process->sid = parent->sid ? parent->sid : parent->pid;
        process->controlling_tty = parent->controlling_tty;
        process->uid = parent->uid;
        process->gid = parent->gid;
        process->effective_uid = parent->effective_uid;
        process->effective_gid = parent->effective_gid;
        process->saved_uid = parent->saved_uid;
        process->saved_gid = parent->saved_gid;
        size_t c = 0;
        while (parent->cwd[c] && c < sizeof(process->cwd) - 1) {
            process->cwd[c] = parent->cwd[c];
            ++c;
        }
        process->cwd[c] = '\0';
        process->cwd_node = parent->cwd_node;
    } else {
        process->pgid = process->pid;
        process->sid = process->pid;
        process->controlling_tty = 1;
        if (tty_foreground_pgid == 0) tty_foreground_pgid = process->pgid;
        process->uid = 0;
        process->gid = 0;
        process->effective_uid = 0;
        process->effective_gid = 0;
        process->saved_uid = 0;
        process->saved_gid = 0;
        process->cwd[0] = '/';
        process->cwd[1] = '\0';
        process->cwd_node = 0;
    }
    if (argc > 0 && argv && argv[0]) {
        set_process_name_from_path(process, argv[0]);
    } else {
        process->name[0] = '\0';
    }
    if (parent) {
        if (xscore_fd_table_clone(&process->file_descriptors,
                                  &parent->file_descriptors) != 0) {
            xscore_fd_table_close_all(&process->file_descriptors);
            process_slot_release(process);
            return 0;
        }
    } else {
        xscore_fd_table_initialize(&process->file_descriptors);
    }
    if (!process_apply_spawn_actions(process, actions, action_count)) {
        xscore_fd_table_close_all(&process->file_descriptors);
        process_image_release(process);
        process_context_release(process);
        process_slot_release(process);
        return 0;
    }
    process->address_space_root = xscore_vmm_address_space_create();
    int image_loaded = process->address_space_root &&
                       load_elf_image(process, load_image, load_image_size,
                                      argc, argv, envc, envp, exec_path,
                                      interpreter_result > 0);
    if (!image_loaded) {
        if (process->address_space_root) {
            release_process_pages(process);
            xscore_vmm_address_space_destroy(process->address_space_root);
        }
        process->address_space_root = 0;
        xscore_fd_table_close_all(&process->file_descriptors);
        process_image_release(process);
        process_context_release(process);
        process_slot_release(process);
        return 0;
    }
    process->thread_id = target_cpu < SMP_MAX_CPUS
        ? xscore_thread_create_on_cpu(process_thread_main, process,
                                      process->address_space_root, target_cpu)
        : xscore_thread_create_in_address_space(
              process_thread_main, process, process->address_space_root);
    if (!process->thread_id ||
        !xscore_thread_bind_fd_table(process->thread_id,
                                     &process->file_descriptors)) {
        release_process_pages(process);
        xscore_vmm_address_space_destroy(process->address_space_root);
        process->address_space_root = 0;
        xscore_fd_table_close_all(&process->file_descriptors);
        process_image_release(process);
        process_context_release(process);
        process_slot_release(process);
        return 0;
    }
    (void)xscore_thread_bind_process(process->thread_id, process);
    process_slot_publish(process);
    serial_process_status("[INFO] Created isolated user process ", process->pid,
                          "\n");
    return process;
}

struct xscore_process *xscore_process_create_elf_args(const uint8_t *image,
                                                      size_t image_size,
                                                      int argc,
                                                      const char *const argv[]) {
    return process_create_elf_args_on_cpu(image, image_size, argc, argv,
                                          0, 0, argc > 0 ? argv[0] : 0,
                                          0, 0, SMP_MAX_CPUS,
                                          UINT32_MAX);
}

struct xscore_process *xscore_process_create_elf(const uint8_t *image,
                                                 size_t image_size) {
    const char *argv[] = {"process", 0};
    return xscore_process_create_elf_args(image, image_size, 1, argv);
}

static struct xscore_process *xscore_process_create_path_args_env_on_cpu_at(
    uint32_t cwd_node, const char *path, int argc,
    const char *const argv[], int envc, const char *const envp[],
    const struct akaos_spawn_file_action *actions, uint32_t action_count,
    uint32_t cpu_index);

struct xscore_process *xscore_process_create_path_args_at(uint32_t cwd_node,
                                                          const char *path,
                                                          int argc,
                                                          const char *const argv[]) {
    return xscore_process_create_path_args_on_cpu_at(cwd_node, path, argc,
                                                    argv, SMP_MAX_CPUS);
}

struct xscore_process *xscore_process_create_path_args_on_cpu_at(
    uint32_t cwd_node, const char *path, int argc,
    const char *const argv[], uint32_t cpu_index) {
    return xscore_process_create_path_args_env_on_cpu_at(
        cwd_node, path, argc, argv, 0, 0, 0, 0, cpu_index);
}

static struct xscore_process *xscore_process_create_path_args_env_on_cpu_at(
    uint32_t cwd_node, const char *path, int argc,
    const char *const argv[], int envc, const char *const envp[],
    const struct akaos_spawn_file_action *actions, uint32_t action_count,
    uint32_t cpu_index) {
    const uint8_t *image;
    size_t image_size;
    uint32_t image_node = UINT32_MAX;
    if (!xscore_vfs_lookup_file_ref_at(cwd_node, path, &image, &image_size,
                                       &image_node)) {
        return 0;
    }
    struct xscore_process *process = process_create_elf_args_on_cpu(
        image, image_size, argc, argv, envc, envp, path, actions, action_count,
        cpu_index, image_node);
    if (process && path) {
        set_process_name_from_path(process, path);
    }
    return process;
}

struct xscore_process *xscore_process_create_path_args(const char *path,
                                                       int argc,
                                                       const char *const argv[]) {
    return xscore_process_create_path_args_at(0, path, argc, argv);
}

struct xscore_process *xscore_process_create_path(const char *path) {
    const char *argv[] = {path, 0};
    return xscore_process_create_path_args(path, 1, argv);
}

int xscore_process_wait(struct xscore_process *process, uint64_t *exit_status) {
    if (!(process_runtime_flags & PROCESS_RUNTIME_INITIALIZED) || !process) return 0;
    uintptr_t process_flags = xscore_spin_lock_irqsave(&process->lock);
    enum xscore_process_state state = process->state;
    if (state != XSCORE_PROCESS_READY && state != XSCORE_PROCESS_RUNNING &&
        state != XSCORE_PROCESS_STOPPED && state != XSCORE_PROCESS_EXITED) {
        xscore_spin_unlock_irqrestore(&process->lock, process_flags);
        return 0;
    }
    if (state == XSCORE_PROCESS_STOPPED) {
        if (exit_status) *exit_status = process->exit_status;
        xscore_spin_unlock_irqrestore(&process->lock, process_flags);
        return 1;
    }
    if (state != XSCORE_PROCESS_EXITED) {
        process->waiter_thread_id = xscore_thread_current_id();
        process->wait_blocked = 1;
    }
    xscore_spin_unlock_irqrestore(&process->lock, process_flags);
    if (state != XSCORE_PROCESS_EXITED) {
        serial_process_status("[INFO] Blocking wait for process ", process->pid,
                              "\n");
        for (;;) {
            process_flags = xscore_spin_lock_irqsave(&process->lock);
            state = process->state;
            xscore_spin_unlock_irqrestore(&process->lock, process_flags);
            if (state == XSCORE_PROCESS_EXITED ||
                state == XSCORE_PROCESS_STOPPED) break;
            if (!xscore_thread_block()) {
                /* A waiter on the BSP may be waiting for a process owned by
                 * an AP.  There is no BSP-local runnable thread to switch
                 * to in that case, so sleep until the local timer/IPI wakes
                 * us and recheck the process state. */
#if defined(__x86_64__) || defined(__i386__)
                __asm__ volatile("sti; hlt" ::: "memory");
#elif defined(__aarch64__)
                __asm__ volatile("msr daifclr, #2; wfe" ::: "memory");
#elif defined(__arm__)
                __asm__ volatile("cpsie i; wfi" ::: "memory");
#elif defined(__riscv)
                __asm__ volatile("csrsi sstatus, 2; wfi" ::: "memory");
#else
                __asm__ volatile("");
#endif
            }
        }
        serial_process_status("[INFO] Waiter resumed for process ", process->pid,
                              "\n");
    }
    process_flags = xscore_spin_lock_irqsave(&process->lock);
    if (exit_status) *exit_status = process->exit_status;
    xscore_spin_unlock_irqrestore(&process->lock, process_flags);
    return 1;
}

int xscore_process_reap(struct xscore_process *process) {
    if (!(process_runtime_flags & PROCESS_RUNTIME_INITIALIZED) || !process) return 0;
    uintptr_t process_flags = xscore_spin_lock_irqsave(&process->lock);
    if (process->state != XSCORE_PROCESS_EXITED) {
        xscore_spin_unlock_irqrestore(&process->lock, process_flags);
        return 0;
    }
    xscore_spin_unlock_irqrestore(&process->lock, process_flags);
    /* The user thread publishes process EXITED just before returning through
     * the thread trampoline.  On SMP the waiter can wake on the BSP during
     * that small interval, so wait for the TCB to reach EXITED before
     * releasing its address space and file/image references. */
    uint64_t thread_id = process->thread_id;
    for (;;) {
        enum xscore_thread_state thread_state = xscore_thread_state(thread_id);
        if (thread_state == XSCORE_THREAD_EXITED) break;
        if (thread_state == XSCORE_THREAD_UNUSED) return 0;
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("sti; hlt" ::: "memory");
#elif defined(__aarch64__)
        __asm__ volatile("msr daifclr, #2; wfe" ::: "memory");
#elif defined(__arm__)
        __asm__ volatile("cpsie i; wfi" ::: "memory");
#elif defined(__riscv)
        __asm__ volatile("csrsi sstatus, 2; wfi" ::: "memory");
#else
        __asm__ volatile("");
#endif
    }
    while (!xscore_thread_reap_process_siblings(process, thread_id)) {
        /* Do not release the shared address space while pthread-style user
         * threads from the process still have runnable or blocked contexts. */
        xscore_thread_yield();
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("sti; hlt" ::: "memory");
#elif defined(__aarch64__)
        __asm__ volatile("msr daifclr, #2; wfe" ::: "memory");
#elif defined(__arm__)
        __asm__ volatile("cpsie i; wfi" ::: "memory");
#elif defined(__riscv)
        __asm__ volatile("csrsi sstatus, 2; wfi" ::: "memory");
#else
        __asm__ volatile("");
#endif
    }
    if (!xscore_thread_reap(thread_id)) return 0;
    xscore_fd_table_close_all(&process->file_descriptors);
    release_process_pages(process);
    process_image_release(process);
    xscore_vmm_address_space_destroy(process->address_space_root);
    process->address_space_root = 0;
    process->thread_id = 0;
    process->parent_pid = 0;
    process->pending_signal = 0;
    process->pending_signal_sender_pid = 0;
    process->pending_signal_sender_uid = 0;
    process->ignored_signals = 0;
    process->signal_mask = 0;
    for (uint32_t s = 0; s <= AKAOS_SIGNAL_MAX; ++s) {
        process->signal_actions[s].sa_handler = AKAOS_SIG_DFL;
        process->signal_actions[s].sa_mask = 0;
        process->signal_actions[s].sa_flags = 0;
        process->signal_actions[s].sa_restorer = 0;
    }
    process->foreground = 0;
    process->signal_trampoline = 0;
    process_context_release(process);
    process->fork_resume = 0;
    process_slot_mark_reaped(process);
    return 1;
}

uint64_t xscore_process_pid(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint64_t pid = process->pid;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return pid;
}

uint64_t xscore_process_parent_pid(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint64_t parent_pid = process->parent_pid;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return parent_pid;
}

uint64_t xscore_process_thread_id(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint64_t thread_id = process->thread_id;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return thread_id;
}

uint64_t xscore_process_address_space_root(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint64_t root = process->address_space_root;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return root;
}

int xscore_process_page_info(const struct xscore_process *process,
                             uint64_t virtual_address, uint64_t *flags,
                             uint64_t *physical_address) {
    if (!process) return 0;
    uintptr_t process_flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint64_t page = align_down(virtual_address);
    for (uint32_t index = 0; index < process->mapped_page_count; ++index) {
        if (process->mapped_pages[index] != page) continue;
        if (flags) *flags = process->mapped_page_flags[index];
        if (physical_address) {
            *physical_address = process->physical_pages[index];
        }
        xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock,
                                      process_flags);
        return 1;
    }
    for (uint32_t index = 0; index < process->mmap_page_count; ++index) {
        if (process->mmap_pages[index] != page) continue;
        if (flags) *flags = process->mmap_page_flags[index];
        if (physical_address) {
            *physical_address = process->mmap_physical_pages[index];
        }
        xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock,
                                      process_flags);
        return 1;
    }
    if (page >= process->heap_start &&
        page < process->heap_start +
                   (uint64_t)process->heap_page_count * PAGE_SIZE) {
        uint32_t index = (uint32_t)((page - process->heap_start) / PAGE_SIZE);
        if (flags) {
            *flags = XSCORE_PAGE_USER | XSCORE_PAGE_WRITABLE |
                     XSCORE_PAGE_NO_EXECUTE;
        }
        if (physical_address) {
            *physical_address = process->heap_physical_pages[index];
        }
        xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock,
                                      process_flags);
        return 1;
    }
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock,
                                  process_flags);
    return 0;
}

enum xscore_process_state xscore_process_state(
    const struct xscore_process *process) {
    return process_state_snapshot(process);
}

int xscore_process_has_descriptor(const struct xscore_process *process,
                                  int descriptor) {
    return process &&
           xscore_fd_table_has(&process->file_descriptors, descriptor);
}

int xscore_process_wait_did_block(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    int blocked = process->wait_blocked != 0;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return blocked;
}

struct xscore_process *xscore_process_current(void) {
    uint64_t tid = xscore_thread_current_id();
    if (!(process_runtime_flags & PROCESS_RUNTIME_INITIALIZED) || tid == 0) return 0;
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        if (process_is_visible(&processes[i]) &&
            (processes[i].thread_id == tid ||
             xscore_thread_current_process() == &processes[i])) {
            return &processes[i];
        }
    }
    return 0;
}

struct xscore_fd_table *xscore_process_fd_table(struct xscore_process *process) {
    return process ? &process->file_descriptors : 0;
}

void xscore_process_user_thread_entry(struct xscore_process *process,
                                      uint64_t entry, uint64_t stack,
                                      uint64_t user_arg0) {
    if (!process || !entry || !stack) {
        xscore_thread_record_user_return(0);
        return;
    }
#if defined(__i386__)
    /* The i386 ring-3 entry shim places its first cdecl argument at
     * user_sp+4 immediately before IRET. A freshly mmap'd stack's top page
     * may still be demand-paged (notably in a fork child after stack reuse),
     * and a kernel-mode store cannot take the normal user page-fault path.
     * Ensure that ABI slot is present and writable before entering assembly. */
    if (stack > UINT64_MAX - sizeof(uint32_t) ||
        !xscore_user_range_prepare(stack + sizeof(uint32_t),
                                   sizeof(uint32_t), 1)) {
        xscore_thread_record_user_return(0);
        return;
    }
#endif
    struct xscore_user_transition *transition =
        xscore_thread_current_user_transition();
    if (!transition) return;
    xscore_syscall_prepare_process();
    transition->user_arg0 = user_arg0;
    xscore_user_enter(entry, stack, transition);
    transition->user_arg0 = 0;
}

const char *xscore_process_cwd(const struct xscore_process *process) {
    return process ? process->cwd : "/";
}

size_t xscore_process_copy_cwd(const struct xscore_process *process,
                               char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    if (!process) {
        buffer[0] = '/';
        if (capacity > 1) buffer[1] = '\0';
        return 1;
    }
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    size_t length = 0;
    while (length + 1 < capacity && process->cwd[length]) {
        buffer[length] = process->cwd[length];
        ++length;
    }
    buffer[length] = '\0';
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return length;
}

uint32_t xscore_process_cwd_node(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint32_t node = process->cwd_node;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return node;
}

const char *xscore_process_get_name(const struct xscore_process *process) {
    if (!process || !process->name[0]) return "process";
    return process->name;
}

void xscore_process_set_name(struct xscore_process *process, const char *name) {
    if (!process) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    if (!name) {
        process->name[0] = '\0';
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return;
    }
    size_t i = 0;
    while (name[i] && i < sizeof(process->name) - 1) {
        process->name[i] = name[i];
        ++i;
    }
    process->name[i] = '\0';
    xscore_spin_unlock_irqrestore(&process->lock, flags);
}

uint32_t xscore_process_uid(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint32_t uid = process->uid;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return uid;
}

uint32_t xscore_process_gid(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint32_t gid = process->gid;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return gid;
}

uint32_t xscore_process_effective_uid(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint32_t uid = process->effective_uid;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return uid;
}

uint32_t xscore_process_effective_gid(const struct xscore_process *process) {
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(
        (xscore_spinlock_t *)&process->lock);
    uint32_t gid = process->effective_gid;
    xscore_spin_unlock_irqrestore((xscore_spinlock_t *)&process->lock, flags);
    return gid;
}

int xscore_process_set_uid(struct xscore_process *process, uint32_t uid) {
    if (!process) return -AKAOS_EINVAL;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    if (process->effective_uid == 0) {
        process->uid = uid;
        process->effective_uid = uid;
        process->saved_uid = uid;
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    if (uid != process->uid && uid != process->saved_uid) {
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return -AKAOS_EPERM;
    }
    process->effective_uid = uid;
    xscore_spin_unlock_irqrestore(&process->lock, flags);
    return 0;
}

int xscore_process_set_gid(struct xscore_process *process, uint32_t gid) {
    if (!process) return -AKAOS_EINVAL;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    if (process->effective_uid == 0) {
        process->gid = gid;
        process->effective_gid = gid;
        process->saved_gid = gid;
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    if (gid != process->gid && gid != process->saved_gid) {
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return -AKAOS_EPERM;
    }
    process->effective_gid = gid;
    xscore_spin_unlock_irqrestore(&process->lock, flags);
    return 0;
}

static int text_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

int xscore_process_set_cwd(struct xscore_process *process, const char *path) {
    if (!process || !path || !*path) return -AKAOS_EINVAL;
    char current_cwd[sizeof(process->cwd)];
    xscore_process_copy_cwd(process, current_cwd, sizeof(current_cwd));
    uint32_t current_node = xscore_process_cwd_node(process);
    uint32_t node;
    int err = xscore_vfs_resolve_path_at(current_node, path, &node);
    if (err != 0) return err;
    struct akaos_stat st;
    if (xscore_vfs_stat_at(current_node, path, &st) != 0 ||
        st.type != AKAOS_DT_DIRECTORY) {
        return -AKAOS_ENOTDIR;
    }
    int access_result = xscore_vfs_access_at(current_node, path, 1);
    if (access_result != 0) return access_result;

    char next_cwd[sizeof(process->cwd)];
    for (size_t i = 0; i < sizeof(next_cwd); ++i) next_cwd[i] = current_cwd[i];
    if (path[0] == '/') {
        size_t l = 0;
        while (path[l] && l < sizeof(next_cwd) - 1) {
            next_cwd[l] = path[l];
            ++l;
        }
        while (l > 1 && next_cwd[l - 1] == '/') --l;
        next_cwd[l] = '\0';
    } else if (text_eq(path, ".")) {
        /* Stay */
    } else if (text_eq(path, "..")) {
        size_t l = 0;
        while (next_cwd[l]) ++l;
        while (l > 1 && next_cwd[l - 1] == '/') --l;
        while (l > 1 && next_cwd[l - 1] != '/') --l;
        if (l > 1 && next_cwd[l - 1] == '/') --l;
        next_cwd[l] = '\0';
    } else {
        size_t l = 0;
        while (next_cwd[l]) ++l;
        if (l > 0 && next_cwd[l - 1] != '/') {
            if (l < sizeof(next_cwd) - 1) next_cwd[l++] = '/';
        }
        size_t tl = 0;
        while (path[tl] && l < sizeof(next_cwd) - 1) {
            next_cwd[l++] = path[tl++];
        }
        while (l > 1 && next_cwd[l - 1] == '/') --l;
        next_cwd[l] = '\0';
    }

    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    for (size_t i = 0; i < sizeof(process->cwd); ++i) {
        process->cwd[i] = next_cwd[i];
    }
    process->cwd_node = node;
    xscore_spin_unlock_irqrestore(&process->lock, flags);
    return 0;
}

int xscore_process_set_cwd_node(struct xscore_process *process,
                                uint32_t node_id) {
    if (!process) return -AKAOS_EINVAL;
    char path[128];
    if (!xscore_vfs_build_node_path(node_id, path, sizeof(path)))
        return -AKAOS_ENOTDIR;
    return xscore_process_set_cwd(process, path);
}

static int64_t xscore_process_spawn_path_env_internal(
    const char *path, int argc, const char *const argv[], int envc,
    const char *const envp[],
    const struct akaos_spawn_file_action *actions, uint32_t action_count,
    uint32_t spawn_flags, uint32_t process_group, uint32_t signal_mask,
    uint32_t signal_default,
    uint64_t *out_pid) {
    const uint32_t supported_spawn_flags = AKAOS_POSIX_SPAWN_RESETIDS |
        AKAOS_POSIX_SPAWN_SETPGROUP | AKAOS_POSIX_SPAWN_SETSIGDEF |
        AKAOS_POSIX_SPAWN_SETSIGMASK;
    if (!path || !*path || argc < 0 || envc < 0) return -AKAOS_EINVAL;
    if ((spawn_flags & ~supported_spawn_flags) != 0U ||
        ((spawn_flags & AKAOS_POSIX_SPAWN_SETPGROUP) != 0U &&
         process_group > INT32_MAX))
        return -AKAOS_EINVAL;
    struct xscore_process *caller = xscore_process_current();
    uint32_t cwd_node = caller ? xscore_process_cwd_node(caller) : 0;
    char caller_cwd[128];
    xscore_process_copy_cwd(caller, caller_cwd, sizeof(caller_cwd));

    uint32_t prog_node;
    uint32_t lookup_cwd = cwd_node;
    const char *final_path = path;
    char bin_buf[128];

    int err = xscore_vfs_resolve_path_at(cwd_node, path, &prog_node);
    if (err != 0) {
        /* If relative name without leading slash or dot, search in /bin/ */
        if (path[0] != '/' && path[0] != '.') {
            bin_buf[0] = '/';
            bin_buf[1] = 'b';
            bin_buf[2] = 'i';
            bin_buf[3] = 'n';
            bin_buf[4] = '/';
            size_t idx = 5;
            const char *s = path;
            while (*s && idx < sizeof(bin_buf) - 1) bin_buf[idx++] = *s++;
            bin_buf[idx] = '\0';
            err = xscore_vfs_resolve_path_at(0, bin_buf, &prog_node);
            if (err == 0) {
                final_path = bin_buf;
                lookup_cwd = 0;
            }
        }
        if (err != 0) return -AKAOS_ENOENT;
    }

    err = xscore_vfs_access_at(lookup_cwd, final_path, 1);
    if (err != 0) return err;

    struct xscore_process *child =
        xscore_process_create_path_args_env_on_cpu_at(
            lookup_cwd, final_path, argc, argv, envc, envp,
            actions, action_count,
            process_default_target_cpu());
    if (!child) return -AKAOS_ENOMEM;

    if (spawn_flags & AKAOS_POSIX_SPAWN_RESETIDS) {
        child->effective_uid = child->uid;
        child->effective_gid = child->gid;
        child->saved_uid = child->uid;
        child->saved_gid = child->gid;
    }
    if (spawn_flags & AKAOS_POSIX_SPAWN_SETPGROUP)
        child->pgid = process_group ? process_group : child->pid;
    if (spawn_flags & AKAOS_POSIX_SPAWN_SETSIGMASK) {
        child->signal_mask = signal_mask &
            ~((UINT32_C(1) << AKAOS_SIGKILL) |
              (UINT32_C(1) << AKAOS_SIGSTOP));
    }
    if (spawn_flags & AKAOS_POSIX_SPAWN_SETSIGDEF) {
        for (uint32_t signal = AKAOS_SIGNAL_MIN;
             signal <= AKAOS_SIGNAL_MAX; ++signal) {
            uint32_t bit = UINT32_C(1) << signal;
            if ((signal_default & bit) == 0U) continue;
            child->signal_actions[signal].sa_handler = AKAOS_SIG_DFL;
            child->signal_actions[signal].sa_mask = 0U;
            child->signal_actions[signal].sa_flags = 0U;
            child->signal_actions[signal].sa_restorer = 0;
            child->ignored_signals &= ~bit;
        }
    }
    struct akaos_stat executable_status;
    if (xscore_vfs_stat_at(lookup_cwd, final_path, &executable_status) == 0) {
        if (executable_status.mode & AKAOS_MODE_ISUID) {
            (void)xscore_process_set_uid(child, executable_status.uid);
        }
        if (executable_status.mode & AKAOS_MODE_ISGID) {
            (void)xscore_process_set_gid(child, executable_status.gid);
        }
    }
    if (caller) {
        (void)xscore_process_set_cwd(child, caller_cwd);
    }
    uint64_t child_pid = xscore_process_pid(child);
    if (out_pid) *out_pid = child_pid;
    return (int64_t)child_pid;
}

int64_t xscore_process_spawn_path(const char *path, int argc,
                                 const char *const argv[], uint64_t *out_pid) {
    return xscore_process_spawn_path_env_internal(path, argc, argv, 0, 0,
                                                  0, 0, 0, 0, 0, 0,
                                                  out_pid);
}

int64_t xscore_process_spawn_path_env(const char *path, int argc,
                                      const char *const argv[], int envc,
                                      const char *const envp[],
                                      const struct akaos_spawn_file_action *actions,
                                      uint32_t action_count,
                                      uint32_t spawn_flags,
                                      uint32_t process_group,
                                      uint32_t signal_mask,
                                      uint32_t signal_default,
                                      uint64_t *out_pid) {
    return xscore_process_spawn_path_env_internal(path, argc, argv, envc,
                                                  envp, actions, action_count,
                                                  spawn_flags, process_group,
                                                  signal_mask, signal_default,
                                                  out_pid);
}

static void process_prepare_exec_return(struct xscore_process *process) {
    if (!process || !process->last_syscall_frame) return;
#if defined(__x86_64__)
    uint64_t *frame = (uint64_t *)process->last_syscall_frame;
    uint64_t cs = frame[16];
    uint64_t ss = frame[19];
    for (uint32_t i = 0; i < 20; ++i) frame[i] = 0;
    frame[15] = process->entry;
    frame[16] = cs;
    frame[17] = UINT64_C(0x202);
    frame[18] = process->stack_top;
    frame[19] = ss;
#elif defined(__i386__)
    uint32_t *frame = (uint32_t *)process->last_syscall_frame;
    uint32_t cs = frame[7];
    uint32_t ss = frame[10];
    for (uint32_t i = 0; i < 11; ++i) frame[i] = 0;
    frame[6] = (uint32_t)process->entry;
    frame[7] = cs;
    frame[8] = UINT32_C(0x202);
    frame[9] = (uint32_t)process->stack_top;
    frame[10] = ss;
#elif defined(__aarch64__)
    uint64_t *frame = (uint64_t *)process->last_syscall_frame;
    for (uint32_t i = 0; i < 32; ++i) frame[i] = 0;
    uint64_t spsr = 0;
    __asm__ volatile("msr elr_el1, %0" : : "r"(process->entry));
    __asm__ volatile("msr sp_el0, %0" : : "r"(process->stack_top));
    __asm__ volatile("msr spsr_el1, %0" : : "r"(spsr));
    __asm__ volatile("isb" ::: "memory");
#elif defined(__arm__)
    uint32_t *frame = (uint32_t *)process->last_syscall_frame;
    for (uint32_t i = 0; i < 15; ++i) frame[i] = 0;
    frame[0] = UINT32_C(0x10);
    /* The ARM32 CRT accepts its initial stack in r2 for compatibility with
     * the direct EL0 entry path, even though exec also installs banked SP_usr. */
    frame[3] = (uint32_t)process->stack_top;
    frame[14] = (uint32_t)process->entry;
    struct xscore_user_transition *transition =
        xscore_thread_current_user_transition();
    if (transition) transition->user_rsp = process->stack_top;
#elif defined(__riscv)
#if __riscv_xlen == 64
    uint64_t *frame = (uint64_t *)process->last_syscall_frame;
    uint64_t sstatus = frame[30];
    for (uint32_t i = 0; i < 32; ++i) frame[i] = 0;
    frame[28] = process->stack_top;
    frame[29] = process->entry;
    frame[30] = sstatus;
#else
    uint32_t *frame = (uint32_t *)process->last_syscall_frame;
    uint32_t sstatus = frame[30];
    for (uint32_t i = 0; i < 32; ++i) frame[i] = 0;
    frame[28] = (uint32_t)process->stack_top;
    frame[29] = (uint32_t)process->entry;
    frame[30] = sstatus;
#endif
#endif
}

static void process_adopt_staged_image(struct xscore_process *process,
                                       struct xscore_process *staged) {
    process->address_space_root = staged->address_space_root;
    process->entry = staged->entry;
    process->stack_top = staged->stack_top;
    process->stack_bottom = staged->stack_bottom;
    process->mapped_page_count = staged->mapped_page_count;
    for (uint32_t i = 0; i < USER_TRACKED_PAGE_CAPACITY; ++i) {
        process->mapped_pages[i] = staged->mapped_pages[i];
        process->physical_pages[i] = staged->physical_pages[i];
        process->mapped_page_flags[i] = staged->mapped_page_flags[i];
        process->mapped_page_cow[i] = staged->mapped_page_cow[i];
    }
    process->heap_start = staged->heap_start;
    process->heap_break = staged->heap_break;
    process->heap_page_count = staged->heap_page_count;
    for (uint32_t i = 0; i < USER_MAX_HEAP_PAGES; ++i) {
        process->heap_physical_pages[i] = staged->heap_physical_pages[i];
        process->heap_page_cow[i] = staged->heap_page_cow[i];
    }
    process->mmap_page_count = staged->mmap_page_count;
    process->mmap_next_address = staged->mmap_next_address;
    for (uint32_t i = 0; i < USER_MAX_MMAP_PAGES; ++i) {
        process->mmap_pages[i] = staged->mmap_pages[i];
        process->mmap_physical_pages[i] = staged->mmap_physical_pages[i];
        process->mmap_page_flags[i] = staged->mmap_page_flags[i];
        process->mmap_protections[i] = staged->mmap_protections[i];
        process->mmap_page_cow[i] = staged->mmap_page_cow[i];
        process->mmap_backings[i] = staged->mmap_backings[i];
        process->mmap_backing_offsets[i] = staged->mmap_backing_offsets[i];
    }
    process->signal_trampoline = staged->signal_trampoline;
    struct process_image_backing *destination = process_image_backing(process);
    struct process_image_backing *source = process_image_backing(staged);
    if (destination && source) {
        destination->image = source->image;
        destination->size = source->size;
        destination->load_base = source->load_base;
        process_dynamic_metadata_copy(&destination->dynamic,
                                      &source->dynamic);
    }
    staged->address_space_root = 0;
    staged->mapped_page_count = 0;
    staged->heap_page_count = 0;
    staged->mmap_page_count = 0;
    staged->mmap_next_address = USER_MMAP_START;
}

int64_t xscore_process_execve(const char *path, int argc,
                              const char *const argv[], int envc,
                              const char *const envp[]) {
    if (!path || !*path || argc < 0 || envc < 0) return -AKAOS_EINVAL;
    struct xscore_process *process = xscore_process_current();
    if (!process || !process->last_syscall_frame) return -AKAOS_EINVAL;

    uint32_t lookup_cwd = process->cwd_node;
    const char *final_path = path;
    char bin_buf[128];
    uint32_t program_node = 0;
    int err = xscore_vfs_resolve_path_at(lookup_cwd, path, &program_node);
    if (err != 0 && path[0] != '/' && path[0] != '.') {
        bin_buf[0] = '/'; bin_buf[1] = 'b'; bin_buf[2] = 'i';
        bin_buf[3] = 'n'; bin_buf[4] = '/';
        size_t index = 5;
        while (*path && index < sizeof(bin_buf) - 1) {
            bin_buf[index++] = *path++;
        }
        bin_buf[index] = '\0';
        err = xscore_vfs_resolve_path_at(0, bin_buf, &program_node);
        if (err == 0) { final_path = bin_buf; lookup_cwd = 0; }
    }
    if (err != 0) return -AKAOS_ENOENT;
    err = xscore_vfs_access_at(lookup_cwd, final_path, 1);
    if (err != 0) return err;

    const uint8_t *image = 0;
    size_t image_size = 0;
    uint32_t image_node = UINT32_MAX;
    if (!xscore_vfs_lookup_file_ref_at(lookup_cwd, final_path, &image,
                                       &image_size, &image_node)) {
        return -AKAOS_ENOENT;
    }

    const uint8_t *load_image = image;
    size_t load_image_size = image_size;
    uint32_t load_image_node = image_node;
    char interpreter_path[ELF_INTERPRETER_PATH_MAX];
    int interpreter_result = elf_interpreter_path(
        image, image_size, interpreter_path, sizeof(interpreter_path));
    if (interpreter_result < 0) {
        xscore_vfs_node_release(image_node);
        return -AKAOS_EINVAL;
    }
    if (interpreter_result > 0 &&
        !xscore_vfs_lookup_file_ref_at(0, interpreter_path,
                                       &load_image, &load_image_size,
                                       &load_image_node)) {
        xscore_vfs_node_release(image_node);
        return -AKAOS_ENOENT;
    }

    struct xscore_process *staged =
        (struct xscore_process *)xscore_heap_allocate(sizeof(*staged));
    if (!staged) {
        xscore_serial_write("[ERROR] execve: staged process allocation failed\n");
        if (load_image_node != image_node) {
            xscore_vfs_node_release(load_image_node);
        }
        xscore_vfs_node_release(image_node);
        return -AKAOS_ENOMEM;
    }
    uint8_t *staged_bytes = (uint8_t *)(void *)staged;
    for (size_t i = 0; i < sizeof(*staged); ++i) staged_bytes[i] = 0;
    staged->heap_start = USER_HEAP_START;
    staged->heap_break = USER_HEAP_START;
    if (!process_context_allocate(staged)) {
        xscore_serial_write("[ERROR] execve: staged context allocation failed\n");
        if (load_image_node != image_node) {
            xscore_vfs_node_release(load_image_node);
        }
        xscore_vfs_node_release(image_node);
        (void)xscore_heap_free(staged);
        return -AKAOS_ENOMEM;
    }
    struct process_image_backing *staged_backing =
        process_image_backing(staged);
    staged_backing->image = 0;
    staged_backing->size = 0;
    staged_backing->load_base = 0;
    staged->image_node_id = load_image_node;
    staged->image_node_ref = 1;
    staged->address_space_root = xscore_vmm_address_space_create();
    int staged_image_loaded = 0;
    if (!staged->address_space_root) {
        xscore_serial_write("[ERROR] execve: address-space allocation failed\n");
    } else {
        staged_image_loaded = load_elf_image(
            staged, load_image, load_image_size, argc, argv,
            envc, envp, final_path, interpreter_result > 0);
        if (!staged_image_loaded)
            xscore_serial_write("[ERROR] execve: ELF image load failed\n");
    }
    if (!staged->address_space_root || !staged_image_loaded) {
        if (staged->address_space_root) {
            release_process_pages(staged);
            xscore_vmm_address_space_destroy(staged->address_space_root);
        }
        xscore_vfs_node_release(load_image_node);
        if (load_image_node != image_node) {
            xscore_vfs_node_release(image_node);
        }
        process_context_release(staged);
        (void)xscore_heap_free(staged);
        return -AKAOS_ENOMEM;
    }

    uint64_t old_root = process->address_space_root;
    if (!xscore_thread_replace_current_address_space(
            staged->address_space_root)) {
        xscore_serial_write("[ERROR] execve: address-space switch failed\n");
        release_process_pages(staged);
        xscore_vmm_address_space_destroy(staged->address_space_root);
        xscore_vfs_node_release(load_image_node);
        if (load_image_node != image_node) {
            xscore_vfs_node_release(image_node);
        }
        process_context_release(staged);
        (void)xscore_heap_free(staged);
        return -AKAOS_ENOMEM;
    }

    /* POSIX exec preserves ordinary descriptors but closes those explicitly
     * marked close-on-exec.  Apply this only after the replacement image is
     * committed, so a failed exec leaves the caller's descriptors untouched. */
    xscore_fd_table_close_cloexec(&process->file_descriptors);

    uint32_t old_image_node = process->image_node_id;
    uint32_t old_image_ref = process->image_node_ref;
    release_process_pages(process);
    if (old_image_ref) xscore_vfs_node_release(old_image_node);
    xscore_vmm_address_space_destroy(old_root);
    process_adopt_staged_image(process, staged);
    process->image_node_id = staged->image_node_id;
    process->image_node_ref = staged->image_node_ref;
    staged->image_node_id = 0;
    staged->image_node_ref = 0;
    if (load_image_node != image_node) {
        /* The staged process keeps the interpreter node.  The main image is
         * reopened by the userspace interpreter through /proc-style file I/O. */
        xscore_vfs_node_release(image_node);
    }
    set_process_name_from_path(process, final_path);
    process->fork_context_size = 0;
    process->fork_resume = 0;
    process->pending_signal = 0;
    process->pending_signal_sender_pid = 0;
    process->pending_signal_sender_uid = 0;
    process->termination_signal = 0U;
    for (uint32_t signal = 1; signal <= AKAOS_SIGNAL_MAX; ++signal) {
        if (process->signal_actions[signal].sa_handler != AKAOS_SIG_IGN) {
            process->signal_actions[signal].sa_handler = AKAOS_SIG_DFL;
            process->signal_actions[signal].sa_mask = 0;
            process->signal_actions[signal].sa_flags = 0;
            process->signal_actions[signal].sa_restorer = 0;
        }
    }

    struct akaos_stat executable_status;
    if (xscore_vfs_stat_at(lookup_cwd, final_path, &executable_status) == 0) {
        if (executable_status.mode & AKAOS_MODE_ISUID)
            process->effective_uid = executable_status.uid;
        if (executable_status.mode & AKAOS_MODE_ISGID)
            process->effective_gid = executable_status.gid;
    }
    process->saved_uid = process->effective_uid;
    process->saved_gid = process->effective_gid;
    process_prepare_exec_return(process);

    process_context_release(staged);
    (void)xscore_heap_free(staged);
    xscore_serial_write("[INFO] POSIX execve process image replacement verified\n");
    return 0;
}

int64_t xscore_process_waitpid(uint64_t pid, uint64_t *out_status) {
    return xscore_process_waitpid_options(pid, out_status, 0);
}

int64_t xscore_process_waitpid_options(uint64_t pid, uint64_t *out_status, uint32_t options) {
    struct xscore_process *target = 0;
    struct xscore_process *caller = xscore_process_current();
    uint64_t caller_pid = xscore_process_pid(caller);
    enum xscore_process_state target_state = XSCORE_PROCESS_UNUSED;
    int any_child = (pid == UINT32_MAX || pid == UINT64_MAX || (int64_t)pid == -1 || (int)pid == -1);
    int has_child = 0;

    uintptr_t table_flags = xscore_spin_lock_irqsave(&process_table_lock);
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        struct xscore_process *candidate = &processes[i];
        uintptr_t process_flags = xscore_spin_lock_irqsave(&candidate->lock);
        enum xscore_process_state state = candidate->state;
        uint64_t candidate_parent_pid = candidate->parent_pid;
        uint64_t candidate_pid = candidate->pid;
        int matches = process_state_is_visible(state) &&
                      (!caller || candidate_parent_pid == caller_pid) &&
                      (any_child || candidate_pid == pid);
        xscore_spin_unlock_irqrestore(&candidate->lock, process_flags);
        if (matches) {
            has_child = 1;
            if (state == XSCORE_PROCESS_EXITED ||
                state == XSCORE_PROCESS_STOPPED) {
                target = candidate;
                target_state = state;
                break;
            }
            if (!target) {
                target = candidate;
                target_state = state;
            }
        }
    }
    xscore_spin_unlock_irqrestore(&process_table_lock, table_flags);

    if (!has_child) return -AKAOS_ECHILD;

    if ((options & 1) && target_state != XSCORE_PROCESS_EXITED &&
        target_state != XSCORE_PROCESS_STOPPED) {
        return 0;
    }

    if (!target) return -AKAOS_ECHILD;
    uint64_t status = 0;
    if (!xscore_process_wait(target, &status)) {
        return -AKAOS_ECHILD;
    }
    enum xscore_process_state final_state = process_state_snapshot(target);
    if (out_status) {
        if (final_state == XSCORE_PROCESS_STOPPED)
            *out_status = status;
        else if (target->termination_signal != 0U)
            *out_status = target->termination_signal & UINT32_C(0x7f);
        else
            *out_status = (status & UINT64_C(0xff)) << 8;
    }
    uint64_t res_pid = xscore_process_pid(target);
    if (final_state == XSCORE_PROCESS_EXITED) {
        xscore_process_reap(target);
    }
    return (int64_t)res_pid;
}

static int valid_signal(uint32_t signal) {
    return signal >= AKAOS_SIGNAL_MIN && signal <= AKAOS_SIGNAL_MAX;
}

void xscore_process_record_termination_signal(uint32_t signal) {
    struct xscore_process *process = xscore_process_current();
    if (!process || !valid_signal(signal)) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    process->termination_signal = signal;
    xscore_spin_unlock_irqrestore(&process->lock, flags);
}

int xscore_process_set_signal_ignored(struct xscore_process *process,
                                      uint32_t signal, int ignored) {
    if (!process || !valid_signal(signal)) return -AKAOS_EINVAL;
    uint32_t mask = UINT32_C(1) << signal;
    int previous = (process->ignored_signals & mask) != 0;
    if (ignored) process->ignored_signals |= mask;
    else process->ignored_signals &= ~mask;
    return previous;
}

int64_t xscore_process_signal(uint64_t pid, uint32_t signal) {
    if (pid == 0 || !valid_signal(signal)) return -AKAOS_EINVAL;
    serial_process_status("[INFO] Signal ", (uint64_t)signal,
                          " delivered to process ");
    serial_process_status("", pid, "\n");
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        struct xscore_process *process = &processes[i];
        if (process->pid != pid || !process_is_visible(process)) continue;
        struct xscore_process *caller = xscore_process_current();
        if (caller && caller->effective_uid != 0 &&
            caller->effective_uid != process->uid &&
            caller->effective_uid != process->effective_uid) {
            return -AKAOS_EPERM;
        }
        if (process->ignored_signals & (UINT32_C(1) << signal)) return 0;

        if (signal == AKAOS_SIGSTOP || signal == AKAOS_SIGTSTP) {
            if (process->state == XSCORE_PROCESS_READY || process->state == XSCORE_PROCESS_RUNNING) {
                process->state = XSCORE_PROCESS_STOPPED;
                process->exit_status = (uint64_t)(0x7f | (signal << 8));
                if (process->waiter_thread_id) {
                    (void)xscore_thread_wake(process->waiter_thread_id);
                }
                return 0;
            }
        } else if (signal == AKAOS_SIGCONT) {
            if (process->state == XSCORE_PROCESS_STOPPED) {
                process->state = XSCORE_PROCESS_READY;
                process->exit_status = 0xffff;
                (void)xscore_thread_wake(process->thread_id);
                return 0;
            }
        }

        uintptr_t signal_flags = xscore_spin_lock_irqsave(&process->lock);
        process->pending_signal = signal;
        process->pending_signal_code = AKAOS_SI_USER;
        process->pending_signal_address = 0;
        process->pending_signal_sender_pid = caller ? caller->pid : 0;
        process->pending_signal_sender_uid = caller ? caller->effective_uid : 0;
        xscore_spin_unlock_irqrestore(&process->lock, signal_flags);
        if (process->thread_id != xscore_thread_current_id()) {
            (void)xscore_thread_wake(process->thread_id);
        }
        return 0;
    }
    return -AKAOS_ECHILD;
}

int xscore_process_signal_foreground(uint32_t signal) {
    if (!valid_signal(signal)) return 0;
    if (tty_foreground_pgid == 0) return 0;

    /* When the shell owns the terminal there is no foreground job to
     * interrupt.  Keep Ctrl+C/Ctrl+Z as line-discipline controls in that
     * state; the shell's own signal disposition is intentionally ignored. */
    if ((signal == AKAOS_SIGINT || signal == AKAOS_SIGTSTP) &&
        tty_foreground_pgid == tty_shell_pgid) return 0;

    /* An interactive shell owns the terminal while it is waiting for a
     * command and deliberately ignores SIGINT/SIGTSTP.  Keep that invariant
     * at the kernel TTY boundary as well: a stray/early signal must clear the
     * shell line, not terminate the login session.  Foreground children are
     * placed in their own process group, so they continue to receive the
     * signal normally. */
    int shell_only_group = 0;
    int non_shell_member = 0;
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        struct xscore_process *member = &processes[i];
        if (!process_is_visible(member) ||
            member->state == XSCORE_PROCESS_EXITED ||
            member->pgid != tty_foreground_pgid) continue;
        if (member->name[0] == 'a' && member->name[1] == 'k' &&
            member->name[2] == 'a' && member->name[3] == 's' &&
            member->name[4] == 'h' && member->name[5] == '\0') {
            shell_only_group = 1;
        } else {
            non_shell_member = 1;
        }
    }
    if (shell_only_group && !non_shell_member) return 0;
    return xscore_process_signal_pgrp(tty_foreground_pgid, signal);
}

uint64_t xscore_process_sid(const struct xscore_process *process) {
    return process ? process->sid : 0;
}

static void proc_append_text(char *buffer, size_t capacity, size_t *used,
                             const char *text) {
    while (*text && *used + 1 < capacity) buffer[(*used)++] = *text++;
    if (capacity) buffer[*used < capacity ? *used : capacity - 1] = '\0';
}

static void proc_append_number(char *buffer, size_t capacity, size_t *used,
                               uint64_t value) {
    char digits[24];
    size_t count = 0;
    if (value == 0) digits[count++] = '0';
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) {
        char character[2] = {digits[--count], '\0'};
        proc_append_text(buffer, capacity, used, character);
    }
}

static const char *proc_state_name(enum xscore_process_state state) {
    if (state == XSCORE_PROCESS_READY) return "ready";
    if (state == XSCORE_PROCESS_RUNNING) return "running";
    if (state == XSCORE_PROCESS_STOPPED) return "stopped";
    if (state == XSCORE_PROCESS_EXITED) return "zombie";
    return "unused";
}

static size_t render_proc_status(struct xscore_process *process, char *buffer,
                                 size_t capacity) {
    if (!buffer || capacity == 0 || !process) return 0;
    size_t used = 0;
#define PROC_FIELD(label, value) do { \
    proc_append_text(buffer, capacity, &used, label); \
    proc_append_number(buffer, capacity, &used, value); \
    proc_append_text(buffer, capacity, &used, "\n"); \
} while (0)
    proc_append_text(buffer, capacity, &used, "Name:\t");
    proc_append_text(buffer, capacity, &used,
                     process->name[0] ? process->name : "process");
    proc_append_text(buffer, capacity, &used, "\nState:\t");
    proc_append_text(buffer, capacity, &used, proc_state_name(process->state));
    proc_append_text(buffer, capacity, &used, "\n");
    PROC_FIELD("Pid:\t", process->pid);
    PROC_FIELD("PPid:\t", process->parent_pid);
    PROC_FIELD("Pgid:\t", process->pgid);
    PROC_FIELD("Sid:\t", process->sid);
    PROC_FIELD("Uid:\t", process->uid);
    PROC_FIELD("Gid:\t", process->gid);
    PROC_FIELD("TtyPgrp:\t", tty_foreground_pgid);
#undef PROC_FIELD
    return used;
}

size_t xscore_process_proc_status(char *buffer, size_t capacity) {
    return render_proc_status(xscore_process_current(), buffer, capacity);
}

size_t xscore_process_proc_status_pid(uint64_t pid, char *buffer,
                                      size_t capacity) {
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        if (processes[index].pid == pid &&
            process_is_visible(&processes[index])) {
            return render_proc_status(&processes[index], buffer, capacity);
        }
    }
    return 0;
}

size_t xscore_process_proc_list(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    size_t used = 0;
    proc_append_text(buffer, capacity, &used, "PID PPID PGID SID STATE NAME\n");
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        struct xscore_process *process = &processes[index];
        if (!process_is_visible(process)) continue;
        proc_append_number(buffer, capacity, &used, process->pid);
        proc_append_text(buffer, capacity, &used, " ");
        proc_append_number(buffer, capacity, &used, process->parent_pid);
        proc_append_text(buffer, capacity, &used, " ");
        proc_append_number(buffer, capacity, &used, process->pgid);
        proc_append_text(buffer, capacity, &used, " ");
        proc_append_number(buffer, capacity, &used, process->sid);
        proc_append_text(buffer, capacity, &used, " ");
        proc_append_text(buffer, capacity, &used,
                         proc_state_name(process->state));
        proc_append_text(buffer, capacity, &used, " ");
        proc_append_text(buffer, capacity, &used,
                         process->name[0] ? process->name : "process");
        proc_append_text(buffer, capacity, &used, "\n");
    }
    return used;
}

size_t xscore_process_get_active_pids(uint64_t *pids_out, size_t max_count) {
    if (!pids_out || max_count == 0) return 0;
    size_t count = 0;
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        struct xscore_process *process = &processes[index];
        if (!process_is_visible(process) || process->pid == 0) continue;
        if (count < max_count) {
            pids_out[count++] = process->pid;
        }
    }
    return count;
}

int xscore_process_pid_info(uint64_t pid, uint32_t *uid, uint32_t *gid,
                            enum xscore_process_state *state) {
    if (pid == 0) return 0;
    for (uint32_t index = 0; index < USER_MAX_PROCESSES; ++index) {
        struct xscore_process *process = &processes[index];
        if (process->pid == pid && process_is_visible(process)) {
            if (uid) *uid = process->effective_uid;
            if (gid) *gid = process->effective_gid;
            if (state) *state = process->state;
            return 1;
        }
    }
    return 0;
}

int64_t xscore_process_setsid(void) {
    struct xscore_process *caller = xscore_process_current();
    if (!caller) return -AKAOS_EINVAL;
    if (caller->pgid == caller->pid) return -AKAOS_EPERM;
    caller->sid = caller->pid;
    caller->pgid = caller->pid;
    caller->controlling_tty = 0;
    return (int64_t)caller->sid;
}

int64_t xscore_process_getsid(uint64_t pid) {
    struct xscore_process *caller = xscore_process_current();
    if (pid == 0) return caller ? (int64_t)caller->sid : -AKAOS_ESRCH;
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        if (process_is_visible(&processes[i]) &&
            processes[i].pid == pid) return (int64_t)processes[i].sid;
    }
    return -AKAOS_ESRCH;
}

int xscore_process_tcsetpgrp(uint64_t pgid) {
    struct xscore_process *caller = xscore_process_current();
    if (!caller || !caller->controlling_tty || pgid == 0) {
        return -AKAOS_ENOTTY;
    }
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        if (process_is_visible(&processes[i]) &&
            processes[i].pgid == pgid && processes[i].sid == caller->sid) {
            tty_foreground_pgid = pgid;
            if (caller->name[0] == 'a' && caller->name[1] == 'k' &&
                caller->name[2] == 'a' && caller->name[3] == 's' &&
                caller->name[4] == 'h' && caller->name[5] == '\0' &&
                pgid == caller->pgid) {
                /* A forked child inherits the shell's name until exec, so do
                 * not mistake its temporary process group for the shell's.
                 * Reuse the slot for a later login shell once the old group
                 * has disappeared. */
                int old_shell_alive = 0;
                if (tty_shell_pgid != 0) {
                    for (uint32_t j = 0; j < USER_MAX_PROCESSES; ++j) {
                        /* The shell blocks in waitpid while a foreground job
                         * runs, so process_is_visible() is false in that
                         * state.  Treat every allocated, non-reaped slot as
                         * alive for shell-group ownership tracking. */
                        if (processes[j].state != XSCORE_PROCESS_UNUSED &&
                            processes[j].state != XSCORE_PROCESS_REAPED &&
                            processes[j].state != XSCORE_PROCESS_ALLOCATING &&
                            processes[j].pgid == tty_shell_pgid &&
                            processes[j].name[0] == 'a' &&
                            processes[j].name[1] == 'k' &&
                            processes[j].name[2] == 'a' &&
                            processes[j].name[3] == 's' &&
                            processes[j].name[4] == 'h' &&
                            processes[j].name[5] == '\0') {
                            old_shell_alive = 1;
                            break;
                        }
                    }
                }
                if (tty_shell_pgid == 0 || pgid == tty_shell_pgid ||
                    !old_shell_alive) {
                    tty_shell_pgid = pgid;
                }
            }
            return 0;
        }
    }
    return -AKAOS_EPERM;
}

uint64_t xscore_process_tcgetpgrp(void) {
    struct xscore_process *caller = xscore_process_current();
    if (!caller || !caller->controlling_tty) return 0;
    return tty_foreground_pgid;
}

static int process_group_exists_in_session(uint64_t pgid, uint64_t sid) {
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        if (process_is_visible(&processes[i]) &&
            processes[i].pgid == pgid && processes[i].sid == sid) return 1;
    }
    return 0;
}

uint64_t xscore_process_pgid(const struct xscore_process *process) {
    return process ? process->pgid : 0;
}

int xscore_process_setpgid(uint64_t pid, uint64_t pgid) {
    struct xscore_process *caller = xscore_process_current();
    if (!caller) return -AKAOS_EINVAL;
    if (pid == 0) pid = caller->pid;
    if (pgid == 0) pgid = pid;

    struct xscore_process *target = 0;
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        if (process_is_visible(&processes[i]) &&
            processes[i].pid == pid) {
            target = &processes[i];
            break;
        }
    }
    if (!target) return -AKAOS_ESRCH;
    if (target != caller && target->parent_pid != caller->pid) return -AKAOS_EPERM;
    if (target->sid != caller->sid || target->pid == target->sid) {
        return -AKAOS_EPERM;
    }
    if (pgid != target->pid &&
        !process_group_exists_in_session(pgid, caller->sid)) {
        return -AKAOS_EPERM;
    }
    target->pgid = pgid;
    return 0;
}

uint64_t xscore_process_getpgid(uint64_t pid) {
    struct xscore_process *caller = xscore_process_current();
    if (pid == 0) return caller ? caller->pgid : 0;
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        if (process_is_visible(&processes[i]) &&
            processes[i].pid == pid) {
            return processes[i].pgid;
        }
    }
    return 0;
}

int xscore_process_signal_pgrp(uint64_t pgid, uint32_t signal) {
    if (!valid_signal(signal)) return 0;
    int delivered = 0;
    for (uint32_t i = 0; i < USER_MAX_PROCESSES; ++i) {
        struct xscore_process *process = &processes[i];
        if (!process_is_visible(process) ||
            process->state == XSCORE_PROCESS_EXITED ||
            process->pgid != pgid ||
            (process->ignored_signals & (UINT32_C(1) << signal))) continue;

        /* akash is the session's interactive shell.  It remains alive when
         * the terminal is idle; Ctrl+C/Ctrl+Z are intended for its foreground
         * child job, never for the shell process itself. */
        if ((signal == AKAOS_SIGINT || signal == AKAOS_SIGTSTP) &&
            process->pgid == tty_shell_pgid &&
            process->name[0] == 'a' && process->name[1] == 'k' &&
            process->name[2] == 'a' && process->name[3] == 's' &&
            process->name[4] == 'h' && process->name[5] == '\0') {
            continue;
        }

        if (signal == AKAOS_SIGSTOP || signal == AKAOS_SIGTSTP) {
            process->state = XSCORE_PROCESS_STOPPED;
            process->exit_status = (uint64_t)(0x7f | (signal << 8));
            if (process->waiter_thread_id) {
                (void)xscore_thread_wake(process->waiter_thread_id);
            }
            delivered = 1;
            continue;
        } else if (signal == AKAOS_SIGCONT) {
            if (process->state == XSCORE_PROCESS_STOPPED) {
                process->state = XSCORE_PROCESS_READY;
                process->exit_status = 0xffff;
                (void)xscore_thread_wake(process->thread_id);
                delivered = 1;
                continue;
            }
        }

        uintptr_t signal_flags = xscore_spin_lock_irqsave(&process->lock);
        process->pending_signal = signal;
        process->pending_signal_code = AKAOS_SI_KERNEL;
        process->pending_signal_address = 0;
        process->pending_signal_sender_pid = 0;
        process->pending_signal_sender_uid = 0;
        xscore_spin_unlock_irqrestore(&process->lock, signal_flags);
        if (process->thread_id != xscore_thread_current_id()) {
            (void)xscore_thread_wake(process->thread_id);
        }
        delivered = 1;
    }
    return delivered;
}

int64_t xscore_process_fork(uint64_t *out_pid) {
    struct xscore_process *parent = xscore_process_current();
    if (!parent) return -AKAOS_EINVAL;
    if (parent->fork_context_size == 0 ||
        parent->fork_context_size > USER_FORK_CONTEXT_SIZE) {
        return -AKAOS_EINVAL;
    }

    struct xscore_process *child = process_slot_reserve();
    if (!child) return -AKAOS_EAGAIN;

    if (parent->image_node_ref &&
        !xscore_vfs_node_acquire(parent->image_node_id)) {
        process_slot_release(child);
        return -AKAOS_ENOENT;
    }
    child->image_node_id = parent->image_node_id;
    child->image_node_ref = parent->image_node_ref;

    child->parent_pid = parent->pid;
    child->pgid = parent->pgid ? parent->pgid : parent->pid;
    child->sid = parent->sid ? parent->sid : parent->pid;
    child->controlling_tty = parent->controlling_tty;
    child->uid = parent->uid;
    child->gid = parent->gid;
    child->effective_uid = parent->effective_uid;
    child->effective_gid = parent->effective_gid;
    child->saved_uid = parent->saved_uid;
    child->saved_gid = parent->saved_gid;
    child->cwd_node = parent->cwd_node;
    for (size_t i = 0; i < sizeof(child->cwd); ++i) child->cwd[i] = parent->cwd[i];
    for (size_t i = 0; i < sizeof(child->name); ++i) child->name[i] = parent->name[i];
    child->ignored_signals = parent->ignored_signals;
    child->signal_mask = parent->signal_mask;
    for (uint32_t s = 0; s <= AKAOS_SIGNAL_MAX; ++s) {
        child->signal_actions[s] = parent->signal_actions[s];
    }
    child->pending_signal = 0;
    child->pending_signal_sender_pid = 0;
    child->pending_signal_sender_uid = 0;
    child->foreground = 0;
    child->signal_trampoline = parent->signal_trampoline;
    child->heap_start = parent->heap_start;
    child->heap_break = parent->heap_break;
    child->entry = parent->entry;
    child->stack_top = parent->stack_top;
    child->stack_bottom = parent->stack_bottom;
    child->thread_id = 0;
    child->waiter_thread_id = 0;
    child->mapped_page_count = 0;
    child->heap_page_count = 0;
    child->mmap_page_count = 0;
    child->mmap_next_address = parent->mmap_next_address;
    child->exit_status = UINT64_MAX;
    child->termination_signal = 0U;
    child->wait_blocked = 0;
    child->fork_context_size = parent->fork_context_size;
    child->fork_resume = 0;
    if (!process_context_allocate(child)) {
        process_image_release(child);
        process_slot_release(child);
        return -AKAOS_ENOMEM;
    }
    for (uint32_t i = 0; i < parent->fork_context_size; ++i) {
        child->fork_context[i] = parent->fork_context[i];
    }
#if defined(__aarch64__)
    for (uint32_t i = USER_FORK_CONTEXT_SIZE;
         i < USER_FORK_CONTEXT_ALLOCATION_SIZE; ++i) {
        child->fork_context[i] = parent->fork_context[i];
    }
#endif
    struct process_image_backing *parent_backing =
        process_image_backing(parent);
    struct process_image_backing *child_backing =
        process_image_backing(child);
    if (!parent_backing || !child_backing) goto fork_failed;
    child_backing->image = parent_backing->image;
    child_backing->size = parent_backing->size;
    child_backing->load_base = parent_backing->load_base;
    process_dynamic_metadata_copy(&child_backing->dynamic,
                                  &parent_backing->dynamic);
#if defined(__x86_64__)
    ((uint64_t *)child->fork_context)[14] = 0;
#elif defined(__i386__)
    /* EAX is restored explicitly by the i386 resume assembly. */
#elif defined(__aarch64__)
    ((uint64_t *)child->fork_context)[0] = 0;
#elif defined(__arm__)
    ((uint32_t *)child->fork_context)[1] = 0;
#elif defined(__riscv) && __riscv_xlen == 64
    ((uint64_t *)child->fork_context)[6] = 0;
#elif defined(__riscv)
    ((uint32_t *)child->fork_context)[6] = 0;
#endif
    child->fork_resume = 1;

    child->address_space_root = xscore_vmm_address_space_create();
    if (!child->address_space_root) {
        process_context_release(child);
        child->fork_context_size = 0;
        process_image_release(child);
        process_slot_release(child);
        return -AKAOS_ENOMEM;
    }

    for (uint32_t i = 0; i < parent->mapped_page_count; ++i) {
        uint64_t vaddr = parent->mapped_pages[i];
        uint64_t src_paddr = parent->physical_pages[i];
        uint64_t original_flags = parent->mapped_page_flags[i];
        child->mapped_pages[i] = vaddr;
        child->physical_pages[i] = 0;
        child->mapped_page_flags[i] = original_flags;
        child->mapped_page_cow[i] = 0;
        child->mapped_page_count = i + 1;
        if (!src_paddr) continue;
        uint64_t shared_flags = original_flags & ~XSCORE_PAGE_WRITABLE;
        uint8_t cow = (original_flags & XSCORE_PAGE_WRITABLE) != 0;
        if (cow && !parent->mapped_page_cow[i]) {
            if (!process_remap_page(parent, vaddr, src_paddr, shared_flags)) {
                goto fork_failed;
            }
            parent->mapped_page_cow[i] = 1;
        }
        if (!xscore_vmm_map_page_in(child->address_space_root, vaddr,
                                    src_paddr, shared_flags)) {
            goto fork_failed;
        }
        xscore_pmm_refcount_inc(src_paddr);
        child->physical_pages[i] = src_paddr;
        child->mapped_page_cow[i] = cow || parent->mapped_page_cow[i];
    }
    for (uint32_t i = 0; i < parent->heap_page_count; ++i) {
        uint64_t vaddr = parent->heap_start + (uint64_t)i * PAGE_SIZE;
        uint64_t src_paddr = parent->heap_physical_pages[i];
        child->heap_physical_pages[i] = 0;
        child->heap_page_cow[i] = 0;
        child->heap_page_count = i + 1;
        if (!src_paddr) continue;
        uint64_t shared_flags = XSCORE_PAGE_USER | XSCORE_PAGE_NO_EXECUTE;
        if (!parent->heap_page_cow[i]) {
            if (!process_remap_page(parent, vaddr, src_paddr, shared_flags)) {
                goto fork_failed;
            }
            parent->heap_page_cow[i] = 1;
        }
        if (!xscore_vmm_map_page_in(child->address_space_root, vaddr,
                                    src_paddr, shared_flags)) {
            goto fork_failed;
        }
        xscore_pmm_refcount_inc(src_paddr);
        child->heap_physical_pages[i] = src_paddr;
        child->heap_page_cow[i] = 1;
    }
    for (uint32_t i = 0; i < parent->mmap_page_count; ++i) {
        uint64_t vaddr = parent->mmap_pages[i];
        uint64_t src_paddr = parent->mmap_physical_pages[i];
        uint64_t original_flags = parent->mmap_page_flags[i];
        child->mmap_pages[i] = vaddr;
        child->mmap_physical_pages[i] = 0;
        child->mmap_page_flags[i] = original_flags;
        child->mmap_protections[i] = parent->mmap_protections[i];
        child->mmap_page_cow[i] = 0;
        child->mmap_backings[i] = parent->mmap_backings[i];
        child->mmap_backing_offsets[i] = parent->mmap_backing_offsets[i];
        mmap_backing_acquire(child->mmap_backings[i]);
        child->mmap_page_count = i + 1;
        if (!src_paddr) continue;

        int is_shared = child->mmap_backings[i] &&
            (child->mmap_backings[i]->flags & MMAP_BACKING_SHARED);
        int accessible = child->mmap_protections[i] != AKAOS_PROT_NONE;
        uint64_t child_flags = original_flags;
        if (!is_shared) {
            child_flags &= ~XSCORE_PAGE_WRITABLE;
            if (accessible && !parent->mmap_page_cow[i]) {
                if (!process_remap_page(parent, vaddr, src_paddr,
                                        child_flags)) goto fork_failed;
            }
            parent->mmap_page_cow[i] = 1;
            child->mmap_page_cow[i] = 1;
        }
        if (accessible &&
            !xscore_vmm_map_page_in(child->address_space_root, vaddr,
                                    src_paddr, child_flags)) {
            goto fork_failed;
        }
        xscore_pmm_refcount_inc(src_paddr);
        child->mmap_physical_pages[i] = src_paddr;
    }

    if (xscore_fd_table_clone(&child->file_descriptors,
                              &parent->file_descriptors) != 0)
        goto fork_failed;

    uint32_t target_cpu = process_default_target_cpu();
    child->thread_id = target_cpu < SMP_MAX_CPUS
        ? xscore_thread_create_on_cpu(process_thread_main, child,
                                      child->address_space_root, target_cpu)
        : xscore_thread_create_in_address_space(
              process_thread_main, child, child->address_space_root);
    if (!child->thread_id ||
        !xscore_thread_bind_fd_table(child->thread_id, &child->file_descriptors)) {
        goto fork_failed_with_descriptors;
    }
    (void)xscore_thread_bind_process(child->thread_id, child);
    process_slot_publish(child);

    if (out_pid) *out_pid = child->pid;
    return 0;

fork_failed_with_descriptors:
    xscore_fd_table_close_all(&child->file_descriptors);
fork_failed:
    release_process_pages(child);
    process_image_release(child);
    xscore_vmm_address_space_destroy(child->address_space_root);
    child->address_space_root = 0;
    process_context_release(child);
    child->fork_resume = 0;
    process_slot_release(child);
    return -AKAOS_ENOMEM;
}

#define XSCORE_SIGFRAME_MAGIC UINT32_C(0x53494746) /* 'SIGF' */

#if defined(__x86_64__)
struct xscore_sigframe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
    uint32_t signal_mask;
    uint32_t magic;
    struct akaos_siginfo siginfo;
};
#elif defined(__i386__)
struct xscore_sigframe {
    uint32_t ebx, ecx, edx, esi, edi, ebp;
    uint32_t eip, cs, eflags, user_esp, user_ss;
    uint32_t eax;
    uint32_t signal_mask;
    uint32_t magic;
    struct akaos_siginfo siginfo;
};
#elif defined(__aarch64__)
struct xscore_sigframe {
    uint64_t x[31];
    uint64_t elr_el1;
    uint64_t sp_el0;
    uint64_t spsr_el1;
    uint32_t signal_mask;
    uint32_t magic;
    struct akaos_siginfo siginfo;
};
#elif defined(__arm__)
struct xscore_sigframe {
    uint32_t r[13];
    uint32_t lr;
    uint32_t pc;
    uint32_t sp;
    uint32_t cpsr;
    uint32_t signal_mask;
    uint32_t magic;
    struct akaos_siginfo siginfo;
};
#elif defined(__riscv) && __riscv_xlen == 64
struct xscore_sigframe {
    uint64_t x[32];
    uint64_t sepc;
    uint64_t sstatus;
    uint32_t signal_mask;
    uint32_t magic;
    struct akaos_siginfo siginfo;
};
#elif defined(__riscv)
struct xscore_sigframe {
    uint32_t x[32];
    uint32_t sepc;
    uint32_t sstatus;
    uint32_t signal_mask;
    uint32_t magic;
    struct akaos_siginfo siginfo;
};
#endif

int64_t xscore_process_sigaction(uint32_t signal,
                                 const struct akaos_sigaction *act,
                                 struct akaos_sigaction *oldact) {
    struct xscore_process *process = xscore_process_current();
    if (!process) return -AKAOS_EINVAL;
    if (signal < AKAOS_SIGNAL_MIN || signal > AKAOS_SIGNAL_MAX) return -AKAOS_EINVAL;
    if (signal == AKAOS_SIGKILL || signal == AKAOS_SIGSTOP) {
        if (act != 0) return -AKAOS_EINVAL;
    }

    if (oldact != 0) {
        if (!xscore_copy_to_user((uint64_t)(uintptr_t)oldact,
                                 &process->signal_actions[signal],
                                 sizeof(struct akaos_sigaction))) {
            return -AKAOS_EFAULT;
        }
    }

    if (act != 0) {
        struct akaos_sigaction kact;
        if (!xscore_copy_from_user(&kact, (uint64_t)(uintptr_t)act,
                                   sizeof(struct akaos_sigaction))) {
            return -AKAOS_EFAULT;
        }
        process->signal_actions[signal] = kact;
        if (kact.sa_handler == AKAOS_SIG_IGN) {
            process->ignored_signals |= (UINT32_C(1) << signal);
            if (process->pending_signal == signal) process->pending_signal = 0;
        } else {
            process->ignored_signals &= ~(UINT32_C(1) << signal);
        }
        if (kact.sa_restorer) {
            process->signal_trampoline = (uint64_t)(uintptr_t)kact.sa_restorer;
        }
    }

    return 0;
}

int64_t xscore_process_update_signal_mask(uint32_t how, uint32_t mask,
                                          uint32_t *old_mask) {
    struct xscore_process *process = xscore_process_current();
    if (!process) return -AKAOS_EINVAL;
    if (how > AKAOS_SIG_SETMASK && how != UINT32_MAX)
        return -AKAOS_EINVAL;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    if (old_mask) *old_mask = process->signal_mask;
    if (how != UINT32_MAX) {
        if (how == AKAOS_SIG_BLOCK) {
            process->signal_mask |= mask;
        } else if (how == AKAOS_SIG_UNBLOCK) {
            process->signal_mask &= ~mask;
        } else {
            process->signal_mask = mask;
        }
        process->signal_mask &=
            ~((UINT32_C(1) << AKAOS_SIGKILL) |
              (UINT32_C(1) << AKAOS_SIGSTOP));
    }
    xscore_spin_unlock_irqrestore(&process->lock, flags);
    return 0;
}

static void process_exit_for_signal(uint32_t signal) {
    xscore_process_record_termination_signal(signal);
    xscore_thread_record_user_exit(UINT64_C(128) + signal);
}

int xscore_process_dispatch_user_signal(void *trap_frame) {
    struct xscore_process *process = xscore_process_current();
    if (!process || process->pending_signal == 0) return 0;
    uint32_t signal = process->pending_signal;
    if (signal < AKAOS_SIGNAL_MIN || signal > AKAOS_SIGNAL_MAX) return 0;
    if (process->signal_mask & (UINT32_C(1) << signal)) return 0;
    struct akaos_sigaction *sa = &process->signal_actions[signal];
    void (*handler)(int) = sa->sa_handler;
    if (handler == AKAOS_SIG_DFL || handler == AKAOS_SIG_IGN) return 0;

    process->pending_signal = 0;

    uint64_t restorer = (uint64_t)(uintptr_t)sa->sa_restorer;
    if (!restorer) restorer = process->signal_trampoline;

    struct xscore_sigframe sf;
    for (size_t i = 0; i < sizeof(sf); ++i) ((uint8_t*)&sf)[i] = 0;
    sf.signal_mask = process->signal_mask;
    sf.magic = XSCORE_SIGFRAME_MAGIC;
    sf.siginfo.si_signo = (int32_t)signal;
    sf.siginfo.si_errno = 0;
    sf.siginfo.si_code = process->pending_signal_code;
    sf.siginfo.reserved = 0;
    sf.siginfo.si_addr = (void *)(uintptr_t)process->pending_signal_address;
    int siginfo_handler = (sa->sa_flags & AKAOS_SA_SIGINFO) != 0U;

#if defined(__x86_64__)
    uint64_t *ctx = trap_frame ? (uint64_t *)trap_frame : (process->last_syscall_frame ? (uint64_t *)process->last_syscall_frame : (uint64_t *)process->fork_context);
    if (!ctx) return 0;
    sf.r15 = ctx[0];
    sf.r14 = ctx[1];
    sf.r13 = ctx[2];
    sf.r12 = ctx[3];
    sf.r11 = ctx[4];
    sf.r10 = ctx[5];
    sf.r9  = ctx[6];
    sf.r8  = ctx[7];
    sf.rsi = ctx[8];
    sf.rdi = ctx[9];
    sf.rbp = ctx[10];
    sf.rdx = ctx[11];
    sf.rcx = ctx[12];
    sf.rbx = ctx[13];
    sf.rax = 0;
    sf.rip = ctx[15];
    sf.cs  = ctx[16];
    sf.rflags = ctx[17];
    sf.rsp = ctx[18];
    sf.ss  = ctx[19];

    uint64_t user_sp = sf.rsp;
    uint64_t sp = (user_sp - sizeof(struct xscore_sigframe)) & ~UINT64_C(0xF);
    if (!xscore_copy_to_user(sp, &sf, sizeof(sf))) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    sp -= 8;
    if (!xscore_copy_to_user(sp, &restorer, 8)) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    ctx[18] = sp;                            /* rsp */
    ctx[15] = (uint64_t)(uintptr_t)handler;  /* rip */
    ctx[9]  = (uint64_t)signal;              /* rdi (arg0) */
    if (siginfo_handler) {
        uint64_t frame_address = sp + sizeof(uint64_t);
        ctx[8] = frame_address + offsetof(struct xscore_sigframe, siginfo);
        ctx[11] = frame_address;
    }
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint64_t *fctx = (uint64_t *)process->fork_context;
        fctx[18] = sp;
        fctx[15] = (uint64_t)(uintptr_t)handler;
        fctx[9] = (uint64_t)signal;
        if (siginfo_handler) {
            uint64_t frame_address = sp + sizeof(uint64_t);
            fctx[8] = frame_address + offsetof(struct xscore_sigframe, siginfo);
            fctx[11] = frame_address;
        }
    }

#elif defined(__i386__)
    uint32_t *ctx = trap_frame ? (uint32_t *)trap_frame : (process->last_syscall_frame ? (uint32_t *)process->last_syscall_frame : (uint32_t *)process->fork_context);
    if (!ctx) return 0;
    sf.ebx = ctx[0];
    sf.ecx = ctx[1];
    sf.edx = ctx[2];
    sf.esi = ctx[3];
    sf.edi = ctx[4];
    sf.ebp = ctx[5];
    sf.eip = ctx[6];
    sf.cs  = ctx[7];
    sf.eflags = ctx[8];
    sf.user_esp = ctx[9];
    sf.user_ss  = ctx[10];
    sf.eax = 0;

    uint32_t user_sp = sf.user_esp;
    uint32_t sp = (user_sp - sizeof(struct xscore_sigframe)) & ~UINT32_C(0xF);
    if (!xscore_copy_to_user((uint64_t)sp, &sf, sizeof(sf))) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    uint32_t call_args[4] = {
        (uint32_t)restorer,
        (uint32_t)signal,
        (uint32_t)(sp + offsetof(struct xscore_sigframe, siginfo)),
        sp,
    };
    uint32_t call_args_size = siginfo_handler ? 4U * sizeof(uint32_t)
                                             : 2U * sizeof(uint32_t);
    sp -= call_args_size;
    if (!xscore_copy_to_user((uint64_t)sp, call_args, call_args_size)) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    ctx[9] = sp;                             /* user_esp */
    ctx[6] = (uint32_t)(uintptr_t)handler;   /* eip */
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint32_t *fctx = (uint32_t *)process->fork_context;
        fctx[9] = sp;
        fctx[6] = (uint32_t)(uintptr_t)handler;
    }

#elif defined(__aarch64__)
    uint64_t *ctx = trap_frame ? (uint64_t *)trap_frame : (process->last_syscall_frame ? (uint64_t *)process->last_syscall_frame : (uint64_t *)process->fork_context);
    if (!ctx) return 0;
    sf.x[0] = 0;
    for (int i = 1; i <= 30; ++i) sf.x[i] = ctx[i + 1];
    __asm__ volatile("mrs %0, elr_el1" : "=r"(sf.elr_el1));
    __asm__ volatile("mrs %0, sp_el0" : "=r"(sf.sp_el0));
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(sf.spsr_el1));

    uint64_t user_sp = sf.sp_el0;
    uint64_t sp = (user_sp - sizeof(struct xscore_sigframe)) & ~UINT64_C(0xF);
    if (!xscore_copy_to_user(sp, &sf, sizeof(sf))) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    ctx[0]  = (uint64_t)signal;             /* x0 = signal */
    if (siginfo_handler) {
        uint64_t frame_address = sp;
        ctx[2] = frame_address + offsetof(struct xscore_sigframe, siginfo);
        ctx[3] = frame_address;
    }
    ctx[31] = restorer;                     /* x30 = restorer */
    __asm__ volatile("msr elr_el1, %0" : : "r"((uint64_t)(uintptr_t)handler));
    __asm__ volatile("msr sp_el0, %0" : : "r"(sp));
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint64_t *fctx = (uint64_t *)process->fork_context;
        fctx[0] = (uint64_t)signal;
        if (siginfo_handler) {
            uint64_t frame_address = sp;
            fctx[2] = frame_address +
                      offsetof(struct xscore_sigframe, siginfo);
            fctx[3] = frame_address;
        }
        fctx[31] = restorer;
    }

#elif defined(__arm__)
    uint32_t *ctx = trap_frame ? (uint32_t *)trap_frame : (process->last_syscall_frame ? (uint32_t *)process->last_syscall_frame : (uint32_t *)process->fork_context);
    if (!ctx) return 0;
    sf.r[0] = 0;
    for (int i = 1; i < 13; ++i) sf.r[i] = ctx[i + 1];
    sf.lr = ctx[14];
    sf.pc = ctx[14];
    struct xscore_user_transition *transition = xscore_thread_current_user_transition();
    sf.sp = transition ? transition->user_rsp : 0;
    sf.cpsr = ctx[0];

    uint32_t user_sp = sf.sp;
    uint32_t sp = (user_sp - sizeof(struct xscore_sigframe)) & ~UINT32_C(0x7);
    if (!xscore_copy_to_user((uint64_t)sp, &sf, sizeof(sf))) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    ctx[1]  = (uint32_t)signal;             /* r0 = signal */
    if (siginfo_handler) {
        uint32_t frame_address = sp;
        ctx[2] = frame_address +
                 (uint32_t)offsetof(struct xscore_sigframe, siginfo);
        ctx[3] = frame_address;
    }
    ctx[14] = (uint32_t)(uintptr_t)handler; /* pc = handler */
    if (transition) transition->user_rsp = sp;
    __asm__ volatile("cps #0x1f; mov lr, %0; cps #0x13" : : "r"((uint32_t)restorer));
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint32_t *fctx = (uint32_t *)process->fork_context;
        fctx[1] = (uint32_t)signal;
        if (siginfo_handler) {
            uint32_t frame_address = sp;
            fctx[2] = frame_address +
                      (uint32_t)offsetof(struct xscore_sigframe, siginfo);
            fctx[3] = frame_address;
        }
        fctx[14] = (uint32_t)(uintptr_t)handler;
    }

#elif defined(__riscv) && __riscv_xlen == 64
    uint64_t *ctx = trap_frame ? (uint64_t *)trap_frame : (process->last_syscall_frame ? (uint64_t *)process->last_syscall_frame : (uint64_t *)process->fork_context);
    if (!ctx) return 0;
    sf.x[0] = 0;
    sf.x[1] = ctx[0];  /* ra */
    sf.x[2] = ctx[28]; /* user sp */
    sf.x[3] = 0;
    sf.x[4] = 0;
    sf.x[5] = ctx[1];  /* t0 */
    sf.x[6] = ctx[2];  /* t1 */
    sf.x[7] = ctx[3];  /* t2 */
    sf.x[8] = ctx[4];  /* s0 */
    sf.x[9] = ctx[5];  /* s1 */
    sf.x[10] = 0;      /* syscall return value = 0 */
    sf.x[11] = ctx[7]; /* a1 */
    sf.x[12] = ctx[8]; /* a2 */
    sf.x[13] = ctx[9]; /* a3 */
    sf.x[14] = ctx[10];/* a4 */
    sf.x[15] = ctx[11];/* a5 */
    sf.x[16] = ctx[12];/* a6 */
    sf.x[17] = ctx[13];/* a7 */
    for (int i = 0; i < 10; ++i) sf.x[18 + i] = ctx[14 + i]; /* s2..s11 */
    sf.x[28] = ctx[24];/* t3 */
    sf.x[29] = ctx[25];/* t4 */
    sf.x[30] = ctx[26];/* t5 */
    sf.x[31] = ctx[27];/* t6 */
    sf.sepc = ctx[29];
    sf.sstatus = 0;

    uint64_t user_sp = ctx[28]; /* user sp */
    uint64_t sp = (user_sp - sizeof(struct xscore_sigframe)) & ~UINT64_C(0xF);
    if (!xscore_copy_to_user(sp, &sf, sizeof(sf))) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    ctx[6]  = (uint64_t)signal;             /* a0 = signal */
    if (siginfo_handler) {
        ctx[7] = sp + offsetof(struct xscore_sigframe, siginfo);
        ctx[8] = sp;
    }
    ctx[0]  = restorer;                     /* ra = restorer */
    ctx[28] = sp;                           /* user sp = sp */
    ctx[29] = (uint64_t)(uintptr_t)handler; /* sepc = handler */
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint64_t *fctx = (uint64_t *)process->fork_context;
        fctx[6] = (uint64_t)signal;
        if (siginfo_handler) {
            fctx[7] = sp + offsetof(struct xscore_sigframe, siginfo);
            fctx[8] = sp;
        }
        fctx[0] = restorer;
        fctx[28] = sp;
        fctx[29] = (uint64_t)(uintptr_t)handler;
    }

#elif defined(__riscv)
    uint32_t *ctx = trap_frame ? (uint32_t *)trap_frame : (process->last_syscall_frame ? (uint32_t *)process->last_syscall_frame : (uint32_t *)process->fork_context);
    if (!ctx) return 0;
    sf.x[0] = 0;
    sf.x[1] = ctx[0];  /* ra */
    sf.x[2] = ctx[28]; /* user sp */
    sf.x[3] = 0;
    sf.x[4] = 0;
    sf.x[5] = ctx[1];  /* t0 */
    sf.x[6] = ctx[2];  /* t1 */
    sf.x[7] = ctx[3];  /* t2 */
    sf.x[8] = ctx[4];  /* s0 */
    sf.x[9] = ctx[5];  /* s1 */
    sf.x[10] = 0;      /* syscall return value = 0 */
    sf.x[11] = ctx[7]; /* a1 */
    sf.x[12] = ctx[8]; /* a2 */
    sf.x[13] = ctx[9]; /* a3 */
    sf.x[14] = ctx[10];/* a4 */
    sf.x[15] = ctx[11];/* a5 */
    sf.x[16] = ctx[12];/* a6 */
    sf.x[17] = ctx[13];/* a7 */
    for (int i = 0; i < 10; ++i) sf.x[18 + i] = ctx[14 + i]; /* s2..s11 */
    sf.x[28] = ctx[24];/* t3 */
    sf.x[29] = ctx[25];/* t4 */
    sf.x[30] = ctx[26];/* t5 */
    sf.x[31] = ctx[27];/* t6 */
    sf.sepc = ctx[29];
    sf.sstatus = 0;

    uint32_t user_sp = ctx[28]; /* user sp */
    uint32_t sp = (user_sp - sizeof(struct xscore_sigframe)) & ~UINT32_C(0xF);
    if (!xscore_copy_to_user((uint64_t)sp, &sf, sizeof(sf))) {
        process_exit_for_signal(AKAOS_SIGSEGV);
        return 0;
    }

    ctx[6]  = (uint32_t)signal;             /* a0 = signal */
    if (siginfo_handler) {
        ctx[7] = sp + (uint32_t)offsetof(struct xscore_sigframe, siginfo);
        ctx[8] = sp;
    }
    ctx[0]  = (uint32_t)restorer;           /* ra = restorer */
    ctx[28] = (uint32_t)sp;                 /* user sp = sp */
    ctx[29] = (uint32_t)(uintptr_t)handler; /* sepc = handler */
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint32_t *fctx = (uint32_t *)process->fork_context;
        fctx[6] = (uint32_t)signal;
        if (siginfo_handler) {
            fctx[7] = sp + (uint32_t)offsetof(struct xscore_sigframe, siginfo);
            fctx[8] = sp;
        }
        fctx[0] = (uint32_t)restorer;
        fctx[28] = (uint32_t)sp;
        fctx[29] = (uint32_t)(uintptr_t)handler;
    }
#endif

    process->signal_mask |= sa->sa_mask;
    if (!(sa->sa_flags & SA_NODEFER)) {
        process->signal_mask |= (UINT32_C(1) << signal);
    }
    if (sa->sa_flags & SA_RESETHAND) {
        sa->sa_handler = AKAOS_SIG_DFL;
    }

    process->fork_resume = 1;
    return 1;
}

int64_t xscore_process_sigreturn(void) {
    struct xscore_process *process = xscore_process_current();
    if (!process || !process->fork_context) return -AKAOS_EINVAL;

    struct xscore_sigframe sf;

#if defined(__x86_64__)
    uint64_t *ctx = process->last_syscall_frame ? (uint64_t *)process->last_syscall_frame : (uint64_t *)process->fork_context;
    uint64_t sp = ctx[18];
    if (!xscore_copy_from_user(&sf, sp, sizeof(sf)) || sf.magic != XSCORE_SIGFRAME_MAGIC) {
        return -AKAOS_EINVAL;
    }
    ctx[0]  = sf.r15;
    ctx[1]  = sf.r14;
    ctx[2]  = sf.r13;
    ctx[3]  = sf.r12;
    ctx[4]  = sf.r11;
    ctx[5]  = sf.r10;
    ctx[6]  = sf.r9;
    ctx[7]  = sf.r8;
    ctx[8]  = sf.rsi;
    ctx[9]  = sf.rdi;
    ctx[10] = sf.rbp;
    ctx[11] = sf.rdx;
    ctx[12] = sf.rcx;
    ctx[13] = sf.rbx;
    ctx[14] = sf.rax;
    ctx[15] = sf.rip;
    ctx[16] = sf.cs;
    ctx[17] = sf.rflags;
    ctx[18] = sf.rsp;
    ctx[19] = sf.ss;
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint64_t *fctx = (uint64_t *)process->fork_context;
        for (int i = 0; i < 20; ++i) fctx[i] = ctx[i];
    }
    process->signal_mask = sf.signal_mask;
    process->fork_resume = 1;
    return (int64_t)sf.rax;

#elif defined(__i386__)
    uint32_t *ctx = process->last_syscall_frame ? (uint32_t *)process->last_syscall_frame : (uint32_t *)process->fork_context;
    uint32_t sp = ctx[9];
    int found = 0;
    if (xscore_copy_from_user(&sf, (uint64_t)(sp + 12), sizeof(sf)) && sf.magic == XSCORE_SIGFRAME_MAGIC) {
        found = 1;
    } else if (xscore_copy_from_user(&sf, (uint64_t)(sp + 4), sizeof(sf)) && sf.magic == XSCORE_SIGFRAME_MAGIC) {
        found = 1;
    } else if (xscore_copy_from_user(&sf, (uint64_t)sp, sizeof(sf)) && sf.magic == XSCORE_SIGFRAME_MAGIC) {
        found = 1;
    }
    if (!found) return -AKAOS_EINVAL;

    ctx[0]  = sf.ebx;
    ctx[1]  = sf.ecx;
    ctx[2]  = sf.edx;
    ctx[3]  = sf.esi;
    ctx[4]  = sf.edi;
    ctx[5]  = sf.ebp;
    ctx[6]  = sf.eip;
    ctx[7]  = sf.cs;
    ctx[8]  = sf.eflags;
    ctx[9]  = sf.user_esp;
    ctx[10] = sf.user_ss;
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint32_t *fctx = (uint32_t *)process->fork_context;
        for (int i = 0; i < 11; ++i) fctx[i] = ctx[i];
    }
    process->signal_mask = sf.signal_mask;
    process->fork_resume = 1;
    return (int64_t)(int32_t)sf.eax;

#elif defined(__aarch64__)
    uint64_t *ctx = process->last_syscall_frame ? (uint64_t *)process->last_syscall_frame : (uint64_t *)process->fork_context;
    uint64_t sp = 0;
    __asm__ volatile("mrs %0, sp_el0" : "=r"(sp));
    if (!xscore_copy_from_user(&sf, sp, sizeof(sf)) || sf.magic != XSCORE_SIGFRAME_MAGIC) {
        return -AKAOS_EINVAL;
    }
    ctx[0] = sf.x[0];
    for (int i = 1; i <= 30; ++i) ctx[i + 1] = sf.x[i];
    __asm__ volatile("msr elr_el1, %0" : : "r"(sf.elr_el1));
    __asm__ volatile("msr sp_el0, %0" : : "r"(sf.sp_el0));
    __asm__ volatile("msr spsr_el1, %0" : : "r"(sf.spsr_el1));
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint64_t *fctx = (uint64_t *)process->fork_context;
        fctx[0] = sf.x[0];
        for (int i = 1; i <= 30; ++i) fctx[i + 1] = sf.x[i];
    }
    process->signal_mask = sf.signal_mask;
    process->fork_resume = 1;
    return (int64_t)sf.x[0];

#elif defined(__arm__)
    uint32_t *ctx = process->last_syscall_frame ? (uint32_t *)process->last_syscall_frame : (uint32_t *)process->fork_context;
    struct xscore_user_transition *transition = xscore_thread_current_user_transition();
    uint32_t sp = transition ? transition->user_rsp : 0;
    if (!xscore_copy_from_user(&sf, (uint64_t)sp, sizeof(sf)) || sf.magic != XSCORE_SIGFRAME_MAGIC) {
        return -AKAOS_EINVAL;
    }
    for (int i = 0; i < 13; ++i) ctx[i + 1] = sf.r[i];
    ctx[14] = sf.pc;
    ctx[0]  = sf.cpsr;
    if (transition) transition->user_rsp = sf.sp;
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint32_t *fctx = (uint32_t *)process->fork_context;
        for (int i = 0; i < 13; ++i) fctx[i + 1] = sf.r[i];
        fctx[14] = sf.pc;
        fctx[0] = sf.cpsr;
    }
    process->signal_mask = sf.signal_mask;
    process->fork_resume = 1;
    return (int64_t)(int32_t)sf.r[0];

#elif defined(__riscv) && __riscv_xlen == 64
    uint64_t *ctx = process->last_syscall_frame ? (uint64_t *)process->last_syscall_frame : (uint64_t *)process->fork_context;
    uint64_t sp = ctx[28];
    if (!xscore_copy_from_user(&sf, sp, sizeof(sf)) || sf.magic != XSCORE_SIGFRAME_MAGIC) {
        return -AKAOS_EINVAL;
    }
    ctx[0] = sf.x[1];  /* ra */
    ctx[1] = sf.x[5];  /* t0 */
    ctx[2] = sf.x[6];  /* t1 */
    ctx[3] = sf.x[7];  /* t2 */
    ctx[4] = sf.x[8];  /* s0 */
    ctx[5] = sf.x[9];  /* s1 */
    ctx[6] = sf.x[10]; /* a0 */
    ctx[7] = sf.x[11]; /* a1 */
    ctx[8] = sf.x[12]; /* a2 */
    ctx[9] = sf.x[13]; /* a3 */
    ctx[10] = sf.x[14];/* a4 */
    ctx[11] = sf.x[15];/* a5 */
    ctx[12] = sf.x[16];/* a6 */
    ctx[13] = sf.x[17];/* a7 */
    for (int i = 0; i < 10; ++i) ctx[14 + i] = sf.x[18 + i]; /* s2..s11 */
    ctx[24] = sf.x[28];/* t3 */
    ctx[25] = sf.x[29];/* t4 */
    ctx[26] = sf.x[30];/* t5 */
    ctx[27] = sf.x[31];/* t6 */
    ctx[28] = sf.x[2]; /* user sp */
    ctx[29] = sf.sepc; /* sepc */
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint64_t *fctx = (uint64_t *)process->fork_context;
        for (int i = 0; i < 32; ++i) fctx[i] = ctx[i];
    }
    process->signal_mask = sf.signal_mask;
    process->fork_resume = 1;
    return (int64_t)sf.x[10];

#elif defined(__riscv)
    uint32_t *ctx = process->last_syscall_frame ? (uint32_t *)process->last_syscall_frame : (uint32_t *)process->fork_context;
    uint32_t sp = ctx[28];
    if (!xscore_copy_from_user(&sf, (uint64_t)sp, sizeof(sf)) || sf.magic != XSCORE_SIGFRAME_MAGIC) {
        return -AKAOS_EINVAL;
    }
    ctx[0] = sf.x[1];  /* ra */
    ctx[1] = sf.x[5];  /* t0 */
    ctx[2] = sf.x[6];  /* t1 */
    ctx[3] = sf.x[7];  /* t2 */
    ctx[4] = sf.x[8];  /* s0 */
    ctx[5] = sf.x[9];  /* s1 */
    ctx[6] = sf.x[10]; /* a0 */
    ctx[7] = sf.x[11]; /* a1 */
    ctx[8] = sf.x[12]; /* a2 */
    ctx[9] = sf.x[13]; /* a3 */
    ctx[10] = sf.x[14];/* a4 */
    ctx[11] = sf.x[15];/* a5 */
    ctx[12] = sf.x[16];/* a6 */
    ctx[13] = sf.x[17];/* a7 */
    for (int i = 0; i < 10; ++i) ctx[14 + i] = sf.x[18 + i]; /* s2..s11 */
    ctx[24] = sf.x[28];/* t3 */
    ctx[25] = sf.x[29];/* t4 */
    ctx[26] = sf.x[30];/* t5 */
    ctx[27] = sf.x[31];/* t6 */
    ctx[28] = sf.x[2]; /* user sp */
    ctx[29] = sf.sepc; /* sepc */
    if (process->fork_context && (void*)ctx != (void*)process->fork_context) {
        uint32_t *fctx = (uint32_t *)process->fork_context;
        for (int i = 0; i < 32; ++i) fctx[i] = ctx[i];
    }
    process->signal_mask = sf.signal_mask;
    process->fork_resume = 1;
    return (int64_t)(int32_t)sf.x[10];
#endif
}

int xscore_process_signal_pending_current(void) {
    struct xscore_process *process = xscore_process_current();
    if (!process || process->pending_signal == 0) return 0;
    uint32_t sig = process->pending_signal;
    if (process->signal_mask & (UINT32_C(1) << sig)) return 0;
    if (process->ignored_signals & (UINT32_C(1) << sig)) {
        process->pending_signal = 0;
        return 0;
    }
    /* POSIX specifies that the default action for SIGCHLD is to ignore it.
     * Keep child waitability independent from whether the parent installed a
     * handler, and do not turn an ordinary child exit into a parent exit. */
    if (sig == AKAOS_SIGCHLD &&
        process->signal_actions[sig].sa_handler == AKAOS_SIG_DFL) {
        process->pending_signal = 0;
        process->pending_signal_sender_pid = 0;
        process->pending_signal_sender_uid = 0;
        return 0;
    }
    return 1;
}

uint32_t xscore_process_pending_signal(const struct xscore_process *process) {
    return process ? process->pending_signal : 0;
}

int xscore_process_signalfd_pending(uint32_t signal_mask) {
    struct xscore_process *process = xscore_process_current();
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    uint32_t signal = process->pending_signal;
    if (signal == 0 || signal > AKAOS_SIGNAL_MAX ||
        (signal_mask & (UINT32_C(1) << signal)) == 0U) {
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    if (process->ignored_signals & (UINT32_C(1) << signal)) {
        process->pending_signal = 0;
        process->pending_signal_sender_pid = 0;
        process->pending_signal_sender_uid = 0;
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    xscore_spin_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int xscore_process_signalfd_take(uint32_t signal_mask, uint32_t *signal_out,
                                 uint64_t *sender_pid,
                                 uint32_t *sender_uid) {
    struct xscore_process *process = xscore_process_current();
    if (!process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&process->lock);
    uint32_t signal = process->pending_signal;
    if (signal == 0 || signal > AKAOS_SIGNAL_MAX ||
        (signal_mask & (UINT32_C(1) << signal)) == 0U) {
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    if (process->ignored_signals & (UINT32_C(1) << signal)) {
        process->pending_signal = 0;
        process->pending_signal_sender_pid = 0;
        process->pending_signal_sender_uid = 0;
        xscore_spin_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    if (signal_out) *signal_out = signal;
    if (sender_pid) *sender_pid = process->pending_signal_sender_pid;
    if (sender_uid) *sender_uid = process->pending_signal_sender_uid;
    process->pending_signal = 0;
    process->pending_signal_sender_pid = 0;
    process->pending_signal_sender_uid = 0;
    xscore_spin_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int xscore_process_take_pending_signal(uint32_t *signal) {
    struct xscore_process *process = xscore_process_current();
    if (!process || process->pending_signal == 0) return 0;
    uint32_t sig = process->pending_signal;
    if (process->signal_mask & (UINT32_C(1) << sig)) return 0;
    if (process->ignored_signals & (UINT32_C(1) << sig)) {
        process->pending_signal = 0;
        return 0;
    }
    process->pending_signal = 0;
    if (signal) *signal = sig;
    return 1;
}

int xscore_process_deliver_pending_signal(uint64_t *irq_frame) {
    if (!irq_frame) return 0;
    struct xscore_process *process = xscore_process_current();
    if (!process || process->pending_signal == 0) return 0;
    uint32_t signal = process->pending_signal;
    if (process->signal_mask & (UINT32_C(1) << signal)) return 0;
    if (process->ignored_signals & (UINT32_C(1) << signal)) {
        process->pending_signal = 0;
        return 0;
    }

    if (signal == AKAOS_SIGCHLD &&
        process->signal_actions[signal].sa_handler == AKAOS_SIG_DFL) {
        process->pending_signal = 0;
        process->pending_signal_sender_pid = 0;
        process->pending_signal_sender_uid = 0;
        return 0;
    }

    /* Check if user handler is registered */
    struct akaos_sigaction *sa = &process->signal_actions[signal];
    if (sa->sa_handler != AKAOS_SIG_DFL && sa->sa_handler != AKAOS_SIG_IGN) {
        return xscore_process_dispatch_user_signal(irq_frame);
    }

    process->pending_signal = 0;
    if (process->signal_trampoline) {
        xscore_process_record_termination_signal(signal);
#if defined(__i386__)
        uint32_t *frame32 = (uint32_t *)irq_frame;
        frame32[8] = UINT32_C(128) + signal;
        frame32[14] = (uint32_t)process->signal_trampoline;
#else
        irq_frame[9] = UINT64_C(128) + signal;
        irq_frame[17] = process->signal_trampoline;
#endif
        return 1;
    }
    return 0;
}

/* Turn a process exit-group request into the normal per-thread exit syscall
 * on interrupt return. This preserves each CPU's IRQ/APIC epilogue and avoids
 * freeing a shared address space while detached workers are still executing. */
int xscore_process_deliver_thread_exit(uint64_t *irq_frame) {
    if (!irq_frame || !xscore_thread_current_exit_requested()) return 0;
    struct xscore_process *process = xscore_process_current();
    if (!process || !process->signal_trampoline) return 0;
#if defined(__i386__)
    uint32_t *frame32 = (uint32_t *)irq_frame;
    frame32[8] = UINT32_C(128) + AKAOS_SIGKILL;
    frame32[14] = (uint32_t)process->signal_trampoline;
#else
    irq_frame[9] = UINT64_C(128) + AKAOS_SIGKILL;
    irq_frame[17] = process->signal_trampoline;
#endif
    return 1;
}

static int mmap_page_is_reserved(const struct xscore_process *process,
                                 uint64_t page) {
    for (uint32_t i = 0; i < process->mmap_page_count; ++i) {
        if (process->mmap_pages[i] == page) return 1;
    }
    return 0;
}

static int mmap_range_is_free(const struct xscore_process *process,
                              uint64_t base, uint64_t page_count) {
    if (!process || page_count == 0 ||
        base < USER_MMAP_START || base >= USER_MMAP_END ||
        page_count > (USER_MMAP_END - base) / PAGE_SIZE) return 0;
    uint64_t range_end = base + page_count * PAGE_SIZE;
    for (uint32_t i = 0; i < process->mmap_page_count; ++i) {
        uint64_t page = process->mmap_pages[i];
        if (page >= base && page < range_end) return 0;
    }
    return 1;
}

static int shm_name_valid(const char *name) {
    if (!name || name[0] != '/' || name[1] == 0) return 0;
    uint32_t length = 1;
    while (name[length]) {
        if (name[length] == '/' || length >= AKAOS_SHM_NAME_MAX) return 0;
        ++length;
    }
    return 1;
}

static int shm_name_equal(const char *left, const char *right) {
    uint32_t i = 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

static void shm_name_copy(char *destination, const char *source) {
    uint32_t i = 0;
    while (source[i] && i < AKAOS_SHM_NAME_MAX) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = 0;
}

static int shm_object_allows(const struct process_shm_object *object,
                             const struct xscore_process *process,
                             int readable, int writable) {
    if (!object || !process) return 0;
    if (process->effective_uid == 0) return 1;
    uint32_t bits;
    if (process->effective_uid == object->owner_uid) {
        bits = (object->mode >> 6) & 7U;
    } else if (process->effective_gid == object->owner_gid) {
        bits = (object->mode >> 3) & 7U;
    } else {
        bits = object->mode & 7U;
    }
    if (readable && !(bits & 4U)) return 0;
    if (writable && !(bits & 2U)) return 0;
    return 1;
}

int64_t xscore_process_shm_open(const char *name, uint64_t flags,
                                uint32_t mode) {
    struct xscore_process *process = xscore_process_current();
    if (!process || !shm_name_valid(name)) return -AKAOS_EINVAL;
    if (flags & ~(UINT64_C(3) | AKAOS_O_CREAT | AKAOS_O_EXCL |
                  AKAOS_O_TRUNC | AKAOS_O_CLOEXEC))
        return -AKAOS_EINVAL;
    uint64_t access = flags & UINT64_C(3);
    if (access > AKAOS_O_RDWR) return -AKAOS_EINVAL;
    int readable = access != AKAOS_O_WRONLY;
    int writable = access != AKAOS_O_RDONLY;

    struct process_shm_object *object = 0;
    uint32_t object_id = 0;
    for (uint32_t i = 0; i < PROCESS_SHM_MAX_OBJECTS; ++i) {
        if (shm_objects[i].in_use && shm_objects[i].linked &&
            shm_name_equal(shm_objects[i].name, name)) {
            object = &shm_objects[i];
            object_id = i;
            break;
        }
    }
    int created = 0;
    if (object) {
        if ((flags & (AKAOS_O_CREAT | AKAOS_O_EXCL)) ==
            (AKAOS_O_CREAT | AKAOS_O_EXCL)) return -AKAOS_EEXIST;
    } else {
        if (!(flags & AKAOS_O_CREAT)) return -AKAOS_ENOENT;
        for (uint32_t i = 0; i < PROCESS_SHM_MAX_OBJECTS; ++i) {
            if (!shm_objects[i].in_use) {
                object = &shm_objects[i];
                object_id = i;
                break;
            }
        }
        if (!object) return -AKAOS_ENFILE;
        object->backing = mmap_backing_create(0, MMAP_BACKING_SHARED, 0);
        if (!object->backing) return -AKAOS_ENOMEM;
        mmap_backing_acquire(object->backing); /* linked object ownership */
        object->in_use = 1;
        object->linked = 1;
        object->descriptor_references = 0;
        object->owner_uid = process->effective_uid;
        object->owner_gid = process->effective_gid;
        object->mode = mode & UINT32_C(0777);
        shm_name_copy(object->name, name);
        created = 1;
    }
    if (!created && !shm_object_allows(object, process, readable, writable)) {
        if (created) shm_object_clear(object);
        return -AKAOS_EACCES;
    }
    if (flags & AKAOS_O_TRUNC) {
        if (!writable || object->backing->references != 1) {
            if (created) shm_object_clear(object);
            return -AKAOS_EINVAL;
        }
        for (uint32_t i = 0; i < USER_MAX_MMAP_PAGES; ++i) {
            if (object->backing->physical_pages[i]) {
                (void)xscore_pmm_refcount_dec(
                    object->backing->physical_pages[i]);
                object->backing->physical_pages[i] = 0;
            }
        }
        object->backing->length = 0;
    }
    int64_t descriptor = xscore_fd_open_shm(
        &process->file_descriptors, object_id, readable, writable,
        (flags & AKAOS_O_CLOEXEC) ? AKAOS_FD_CLOEXEC : 0U);
    if (descriptor < 0) {
        if (created) shm_object_clear(object);
        return descriptor;
    }
    ++object->descriptor_references;
    return descriptor;
}

int64_t xscore_process_memfd_create(const char *name, uint32_t name_length,
                                   uint32_t flags) {
    struct xscore_process *process = xscore_process_current();
    if (!process || (!name && name_length != 0U) ||
        name_length > AKAOS_MEMFD_NAME_MAX ||
        (flags & ~(AKAOS_MFD_CLOEXEC | AKAOS_MFD_ALLOW_SEALING)) != 0U)
        return -AKAOS_EINVAL;

    struct process_shm_object *object = 0;
    uint32_t object_id = 0;
    for (uint32_t i = 0; i < PROCESS_SHM_MAX_OBJECTS; ++i) {
        if (!shm_objects[i].in_use) {
            object = &shm_objects[i];
            object_id = i;
            break;
        }
    }
    if (!object) return -AKAOS_ENFILE;

    struct process_mmap_backing *backing = mmap_backing_create(
        0, MMAP_BACKING_SHARED | MMAP_BACKING_MEMFD, 0);
    if (!backing) return -AKAOS_ENOMEM;
    mmap_backing_acquire(backing); /* anonymous memfd object ownership */
    backing->sealable = (flags & AKAOS_MFD_ALLOW_SEALING) != 0U;
    backing->seals = backing->sealable ? 0U : AKAOS_F_SEAL_SEAL;

    object->in_use = 1;
    object->linked = 0;
    object->descriptor_references = 0;
    object->owner_uid = process->effective_uid;
    object->owner_gid = process->effective_gid;
    object->mode = UINT32_C(0600);
    object->name[0] = 0;
    size_t stored_name_length = name_length;
    if (stored_name_length >= sizeof(object->name))
        stored_name_length = sizeof(object->name) - 1U;
    for (size_t i = 0; i < stored_name_length; ++i)
        object->name[i] = name[i];
    object->name[stored_name_length] = 0;
    object->backing = backing;

    int64_t descriptor = xscore_fd_open_shm(
        &process->file_descriptors, object_id, 1, 1,
        (flags & AKAOS_MFD_CLOEXEC) ? AKAOS_FD_CLOEXEC : 0U);
    if (descriptor < 0) {
        shm_object_clear(object);
        return descriptor;
    }
    ++object->descriptor_references;
    return descriptor;
}

void xscore_process_shm_descriptor_release(uint32_t object_id) {
    if (object_id >= PROCESS_SHM_MAX_OBJECTS) return;
    struct process_shm_object *object = &shm_objects[object_id];
    if (!object->in_use || object->descriptor_references == 0) return;
    --object->descriptor_references;
    shm_object_maybe_destroy(object);
}

int64_t xscore_process_shm_unlink(const char *name) {
    struct xscore_process *process = xscore_process_current();
    if (!process || !shm_name_valid(name)) return -AKAOS_EINVAL;
    for (uint32_t i = 0; i < PROCESS_SHM_MAX_OBJECTS; ++i) {
        struct process_shm_object *object = &shm_objects[i];
        if (!object->in_use || !object->linked ||
            !shm_name_equal(object->name, name)) continue;
        if (process->effective_uid != 0 &&
            process->effective_uid != object->owner_uid) return -AKAOS_EPERM;
        object->linked = 0;
        object->name[0] = 0;
        shm_object_maybe_destroy(object);
        return 0;
    }
    return -AKAOS_ENOENT;
}

int64_t xscore_process_ftruncate(int descriptor, uint64_t length) {
    struct xscore_process *process = xscore_process_current();
    if (!process) return -AKAOS_EINVAL;
    uint32_t object_id = 0;
    int writable = 0;
    if (!xscore_fd_shm_state(&process->file_descriptors, descriptor,
                             &object_id, 0, &writable)) {
        uint32_t node_id = 0;
        if (!xscore_fd_vnode_state(&process->file_descriptors, descriptor,
                                   &node_id, 0, &writable) || !writable)
            return -AKAOS_EBADF;
        return xscore_vfs_truncate_node(node_id, length);
    }
    if (!writable || object_id >= PROCESS_SHM_MAX_OBJECTS)
        return -AKAOS_EBADF;
    if (length > AKAOS_SHM_MAX_SIZE) return -AKAOS_EINVAL;
    struct process_shm_object *object = &shm_objects[object_id];
    if (!object->in_use || !object->backing) return -AKAOS_EINVAL;
    /* Growing a shared backing preserves every existing mapping. Shrinking
     * can invalidate pages still visible through those mappings, so it still
     * requires exclusive backing ownership. */
    uint32_t seals = __atomic_load_n(&object->backing->seals,
                                     __ATOMIC_ACQUIRE);
    if ((length < object->backing->length &&
         (seals & AKAOS_F_SEAL_SHRINK)) ||
        (length > object->backing->length &&
         (seals & AKAOS_F_SEAL_GROW))) return -AKAOS_EPERM;
    if (object->backing->references != 1 &&
        length < object->backing->length) return -AKAOS_EINVAL;
    uint32_t keep_pages = (uint32_t)((length + PAGE_SIZE - 1) / PAGE_SIZE);
    if (length < object->backing->length &&
        (length & (PAGE_SIZE - 1U)) != 0U && keep_pages != 0U &&
        object->backing->physical_pages[keep_pages - 1U] != 0U) {
        uint8_t *tail = (uint8_t *)(uintptr_t)
            object->backing->physical_pages[keep_pages - 1U];
        for (uint64_t index = length & (PAGE_SIZE - 1U);
             index < PAGE_SIZE; ++index) tail[index] = 0;
    }
    for (uint32_t i = keep_pages; i < USER_MAX_MMAP_PAGES; ++i) {
        if (object->backing->physical_pages[i]) {
            (void)xscore_pmm_refcount_dec(object->backing->physical_pages[i]);
            object->backing->physical_pages[i] = 0;
        }
    }
    object->backing->length = length;
    return 0;
}

int64_t xscore_process_shm_size(uint32_t object_id) {
    if (object_id >= PROCESS_SHM_MAX_OBJECTS) return -AKAOS_EBADF;
    struct process_shm_object *object = &shm_objects[object_id];
    if (!object->in_use || !object->backing) return -AKAOS_EBADF;
    if (object->backing->length > INT64_MAX) return -AKAOS_EOVERFLOW;
    return (int64_t)object->backing->length;
}

int64_t xscore_process_shm_stat(uint32_t object_id,
                                struct akaos_stat *status) {
    if (!status) return -AKAOS_EFAULT;
    if (object_id >= PROCESS_SHM_MAX_OBJECTS) return -AKAOS_EBADF;
    struct process_shm_object *object = &shm_objects[object_id];
    if (!object->in_use || !object->backing) return -AKAOS_EBADF;
    status->inode = (uint64_t)object_id + 1U;
    status->size = object->backing->length;
    status->mode = AKAOS_MODE_REGULAR | (object->mode & 07777U);
    status->type = AKAOS_DT_REGULAR;
    status->uid = object->owner_uid;
    status->gid = object->owner_gid;
    return 0;
}

int64_t xscore_process_shm_read(uint32_t object_id, uint64_t offset,
                                void *buffer, uint64_t length) {
    if (object_id >= PROCESS_SHM_MAX_OBJECTS) return -AKAOS_EBADF;
    if (!buffer && length != 0U) return -AKAOS_EFAULT;
    if (length == 0U) return 0;
    struct process_shm_object *object = &shm_objects[object_id];
    struct process_mmap_backing *backing = object->backing;
    if (!object->in_use || !backing) return -AKAOS_EBADF;
    if (offset >= backing->length) return 0;
    uint64_t available = backing->length - offset;
    if (length > available) length = available;

    uint8_t *destination = (uint8_t *)buffer;
    uint64_t completed = 0U;
    while (completed < length) {
        uint64_t file_offset = offset + completed;
        uint64_t page_index = file_offset / PAGE_SIZE;
        size_t page_offset = (size_t)(file_offset & (PAGE_SIZE - 1U));
        uint64_t chunk = PAGE_SIZE - page_offset;
        if (chunk > length - completed) chunk = length - completed;
        uint64_t physical = page_index < USER_MAX_MMAP_PAGES
                                ? backing->physical_pages[page_index]
                                : 0U;
        if (physical != 0U) {
            const uint8_t *source = (const uint8_t *)(uintptr_t)physical;
            for (uint64_t index = 0; index < chunk; ++index) {
                destination[completed + index] = source[page_offset + index];
            }
        } else {
            for (uint64_t index = 0; index < chunk; ++index) {
                destination[completed + index] = 0;
            }
        }
        completed += chunk;
    }
    return (int64_t)completed;
}

int64_t xscore_process_shm_write(uint32_t object_id, uint64_t offset,
                                 const void *buffer, uint64_t length) {
    if (object_id >= PROCESS_SHM_MAX_OBJECTS) return -AKAOS_EBADF;
    if (!buffer && length != 0U) return -AKAOS_EFAULT;
    if (length == 0U) return 0;
    struct process_shm_object *object = &shm_objects[object_id];
    struct process_mmap_backing *backing = object->backing;
    if (!object->in_use || !backing) return -AKAOS_EBADF;

    uint32_t seals = __atomic_load_n(&backing->seals, __ATOMIC_ACQUIRE);
    if (seals & (AKAOS_F_SEAL_WRITE | AKAOS_F_SEAL_FUTURE_WRITE))
        return -AKAOS_EPERM;
    if (offset >= AKAOS_SHM_MAX_SIZE) return -AKAOS_ENOSPC;
    uint64_t available = AKAOS_SHM_MAX_SIZE - offset;
    if (length > available) length = available;
    if (offset + length > backing->length &&
        (seals & AKAOS_F_SEAL_GROW)) return -AKAOS_EPERM;

    const uint8_t *source = (const uint8_t *)buffer;
    uint64_t completed = 0U;
    while (completed < length) {
        uint64_t file_offset = offset + completed;
        uint64_t page_index = file_offset / PAGE_SIZE;
        size_t page_offset = (size_t)(file_offset & (PAGE_SIZE - 1U));
        uint64_t chunk = PAGE_SIZE - page_offset;
        if (chunk > length - completed) chunk = length - completed;
        if (page_index >= USER_MAX_MMAP_PAGES) break;
        uint64_t physical = backing->physical_pages[page_index];
        if (physical == 0U) {
            physical = xscore_pmm_allocate_page();
            if (physical == 0U) {
                return completed != 0U ? (int64_t)completed :
                                         -AKAOS_ENOMEM;
            }
            zero_page(physical);
            backing->physical_pages[page_index] = physical;
        }
        uint8_t *destination = (uint8_t *)(uintptr_t)physical;
        for (uint64_t index = 0; index < chunk; ++index) {
            destination[page_offset + index] = source[completed + index];
        }
        completed += chunk;
    }
    if (completed == 0U) return -AKAOS_ENOSPC;
    uint64_t end = offset + completed;
    if (end > backing->length) backing->length = end;
    return (int64_t)completed;
}

static int memfd_has_writable_mapping(
    const struct process_mmap_backing *backing) {
    if (!backing) return 0;
    for (uint32_t process_index = 0; process_index < USER_MAX_PROCESSES;
         ++process_index) {
        const struct xscore_process *process = &processes[process_index];
        for (uint32_t mapping_index = 0;
             mapping_index < process->mmap_page_count; ++mapping_index) {
            if (process->mmap_backings[mapping_index] == backing &&
                (process->mmap_protections[mapping_index] &
                 AKAOS_PROT_WRITE)) return 1;
        }
    }
    return 0;
}

int64_t xscore_process_shm_fcntl(int descriptor, int command,
                                uint64_t argument) {
    struct xscore_process *process = xscore_process_current();
    if (!process) return -AKAOS_EINVAL;
    uint32_t object_id = 0;
    if (!xscore_fd_shm_state(&process->file_descriptors, descriptor,
                             &object_id, 0, 0) ||
        object_id >= PROCESS_SHM_MAX_OBJECTS) return -AKAOS_EBADF;
    struct process_shm_object *object = &shm_objects[object_id];
    struct process_mmap_backing *backing = object->backing;
    if (!object->in_use || !backing ||
        (backing->flags & MMAP_BACKING_MEMFD) == 0U)
        return -AKAOS_EINVAL;

    if (command == AKAOS_F_GET_SEALS) {
        return __atomic_load_n(&backing->seals, __ATOMIC_ACQUIRE);
    }
    if (command != AKAOS_F_ADD_SEALS ||
        (argument & ~(uint64_t)(AKAOS_F_SEAL_SEAL |
                                 AKAOS_F_SEAL_SHRINK |
                                 AKAOS_F_SEAL_GROW |
                                 AKAOS_F_SEAL_WRITE |
                                 AKAOS_F_SEAL_FUTURE_WRITE)) != 0U)
        return -AKAOS_EINVAL;
    if (!backing->sealable) return -AKAOS_EPERM;

    uint32_t seals = __atomic_load_n(&backing->seals, __ATOMIC_ACQUIRE);
    if (seals & AKAOS_F_SEAL_SEAL) return -AKAOS_EPERM;
    if ((argument & AKAOS_F_SEAL_WRITE) &&
        memfd_has_writable_mapping(backing)) return -AKAOS_EBUSY;
    __atomic_fetch_or(&backing->seals, (uint32_t)argument, __ATOMIC_ACQ_REL);
    return 0;
}

int64_t xscore_process_mmap(const struct akaos_mmap_args *arguments) {
    struct xscore_process *process = xscore_process_current();
    if (!process || !arguments || arguments->length == 0) {
        return -AKAOS_EINVAL;
    }
    uint64_t mapping_type = arguments->flags &
                            (AKAOS_MAP_PRIVATE | AKAOS_MAP_SHARED);
    if ((arguments->protection & ~(uint64_t)(AKAOS_PROT_READ |
                                              AKAOS_PROT_WRITE |
                                              AKAOS_PROT_EXEC)) != 0 ||
        (mapping_type != AKAOS_MAP_PRIVATE &&
         mapping_type != AKAOS_MAP_SHARED) ||
        (arguments->flags & ~(uint64_t)(AKAOS_MAP_PRIVATE |
                                        AKAOS_MAP_SHARED |
                                        AKAOS_MAP_FIXED |
                                        AKAOS_MAP_ANONYMOUS)) != 0 ||
        (arguments->offset & (PAGE_SIZE - 1)) != 0 ||
        arguments->offset > (uint64_t)INT64_MAX) {
        return -AKAOS_EINVAL;
    }
    if (arguments->length > UINT64_MAX - (PAGE_SIZE - 1)) {
        return -AKAOS_EINVAL;
    }
    uint64_t rounded_length =
        (arguments->length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t page_count = rounded_length / PAGE_SIZE;
    if (page_count == 0 || page_count > USER_MAX_MMAP_PAGES ||
        process->mmap_page_count + page_count > USER_MAX_MMAP_PAGES) {
        return -AKAOS_ENOMEM;
    }

    uint64_t base = 0;
    if (arguments->flags & AKAOS_MAP_FIXED) {
        if ((arguments->address & (PAGE_SIZE - 1)) != 0 ||
            !mmap_range_is_free(process, arguments->address, page_count)) {
            return -AKAOS_EINVAL;
        }
        base = arguments->address;
    } else {
        uint64_t hint = arguments->address & ~(PAGE_SIZE - 1);
        if (hint && mmap_range_is_free(process, hint, page_count)) base = hint;
        if (!base) {
            uint64_t search_start = process->mmap_next_address;
            if (search_start < USER_MMAP_START ||
                search_start >= USER_MMAP_END)
                search_start = USER_MMAP_START;
            for (uint32_t pass = 0; pass < 2 && !base; ++pass) {
                uint64_t candidate = pass == 0 ? search_start : USER_MMAP_START;
                uint64_t search_end = pass == 0 ? USER_MMAP_END : search_start;
                while (candidate < search_end) {
                    if (mmap_range_is_free(process, candidate, page_count)) {
                        base = candidate;
                        break;
                    }
                    candidate += PAGE_SIZE;
                }
            }
        }
        if (!base) return -AKAOS_ENOMEM;
    }

    struct process_mmap_backing *backing = 0;
    uint64_t backing_base_offset = 0;
    if (arguments->flags & AKAOS_MAP_ANONYMOUS) {
        if (mapping_type == AKAOS_MAP_SHARED) {
            backing = mmap_backing_create(rounded_length,
                                          MMAP_BACKING_SHARED, 0);
            if (!backing) return -AKAOS_ENOMEM;
        }
    } else {
        if (arguments->descriptor < 0 || arguments->descriptor > INT32_MAX) {
            return -AKAOS_EBADF;
        }
        uint32_t shm_object_id = 0;
        int shm_readable = 0;
        int shm_writable = 0;
        if (xscore_fd_shm_state(&process->file_descriptors,
                                (int)arguments->descriptor,
                                &shm_object_id, &shm_readable,
                                &shm_writable)) {
            if (shm_object_id >= PROCESS_SHM_MAX_OBJECTS ||
                !shm_objects[shm_object_id].in_use ||
                !shm_objects[shm_object_id].backing ||
                ((arguments->protection & AKAOS_PROT_READ) &&
                 !shm_readable) ||
                (mapping_type == AKAOS_MAP_SHARED &&
                 (arguments->protection & AKAOS_PROT_WRITE) &&
                 !shm_writable)) return -AKAOS_EACCES;
            struct process_mmap_backing *shm_backing =
                shm_objects[shm_object_id].backing;
            uint64_t rounded_backing_length =
                (shm_backing->length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (arguments->offset > shm_backing->length ||
                rounded_length > rounded_backing_length - arguments->offset) {
                return -AKAOS_EINVAL;
            }
            if (mapping_type == AKAOS_MAP_SHARED) {
                uint32_t seals = __atomic_load_n(&shm_backing->seals,
                                                 __ATOMIC_ACQUIRE);
                if ((arguments->protection & AKAOS_PROT_WRITE) &&
                    (seals & (AKAOS_F_SEAL_WRITE |
                              AKAOS_F_SEAL_FUTURE_WRITE)))
                    return -AKAOS_EPERM;
                backing = shm_backing;
                backing_base_offset = arguments->offset;
            } else {
                backing = mmap_backing_create(rounded_length, 0, 1);
                if (!backing) return -AKAOS_ENOMEM;
                uint64_t copied = 0U;
                int64_t result = xscore_process_shm_read(
                    shm_object_id, arguments->offset, backing->data,
                    arguments->length);
                if (result < 0) {
                    (void)xscore_heap_free(backing);
                    return result;
                }
                copied = (uint64_t)result;
                if (copied > arguments->length) {
                    (void)xscore_heap_free(backing);
                    return -AKAOS_EIO;
                }
            }
        } else {
            uint32_t node_id = 0;
            int readable = 0;
            if (!xscore_fd_vnode_state(&process->file_descriptors,
                                       (int)arguments->descriptor,
                                       &node_id, &readable, 0) || !readable) {
                return -AKAOS_EBADF;
            }
            uint32_t backing_flags = 0;
            if (mapping_type == AKAOS_MAP_SHARED) {
                if ((arguments->protection & AKAOS_PROT_WRITE) != 0U) {
                    return -AKAOS_EACCES;
                }
                if (!xscore_vfs_node_is_immutable_regular(node_id)) {
                    return -AKAOS_EINVAL;
                }
                backing_flags |= MMAP_BACKING_IMMUTABLE_FILE;
            }
            backing = mmap_backing_create(rounded_length, backing_flags, 1);
            if (!backing) return -AKAOS_ENOMEM;
            uint64_t bytes_read = 0;
            if (!xscore_vfs_read_node(node_id, arguments->offset,
                                      backing->data, arguments->length,
                                      &bytes_read)) {
                (void)xscore_heap_free(backing);
                return -AKAOS_EINVAL;
            }
        }
    }

    uint64_t page_flags = XSCORE_PAGE_USER;
    if (arguments->protection & AKAOS_PROT_WRITE) {
        page_flags |= XSCORE_PAGE_WRITABLE;
    }
    if ((arguments->protection & AKAOS_PROT_EXEC) == 0) {
        page_flags |= XSCORE_PAGE_NO_EXECUTE;
    }
    for (uint64_t i = 0; i < page_count; ++i) {
        uint32_t slot = process->mmap_page_count++;
        process->mmap_pages[slot] = base + i * PAGE_SIZE;
        process->mmap_physical_pages[slot] = 0;
        process->mmap_page_flags[slot] = page_flags;
        process->mmap_protections[slot] = (uint8_t)arguments->protection;
        process->mmap_page_cow[slot] = 0;
        process->mmap_backings[slot] = backing;
        process->mmap_backing_offsets[slot] =
            backing_base_offset + i * PAGE_SIZE;
        mmap_backing_acquire(backing);
    }
    uint64_t next_address = base + rounded_length;
    process->mmap_next_address = next_address >= USER_MMAP_END
                                    ? USER_MMAP_START : next_address;
    return (int64_t)base;
}

int64_t xscore_process_munmap(uint64_t address, uint64_t length) {
    struct xscore_process *process = xscore_process_current();
    if (!process || length == 0 || (address & (PAGE_SIZE - 1)) != 0 ||
        length > UINT64_MAX - (PAGE_SIZE - 1)) return -AKAOS_EINVAL;
    uint64_t rounded_length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (address > UINT64_MAX - rounded_length) return -AKAOS_EINVAL;
    uint64_t end = address + rounded_length;
    if (address >= USER_MMAP_START && address < USER_MMAP_END &&
        (process->mmap_next_address < USER_MMAP_START ||
         address < process->mmap_next_address)) {
        process->mmap_next_address = address;
    }

    uint32_t i = 0;
    while (i < process->mmap_page_count) {
        uint64_t page = process->mmap_pages[i];
        if (page < address || page >= end) {
            ++i;
            continue;
        }
        if (process->mmap_physical_pages[i]) {
            (void)xscore_vmm_unmap_page_in(process->address_space_root,
                                           page, 0);
            (void)xscore_pmm_refcount_dec(process->mmap_physical_pages[i]);
        }
        mmap_backing_release(process->mmap_backings[i]);
        uint32_t last = --process->mmap_page_count;
        if (i != last) {
            process->mmap_pages[i] = process->mmap_pages[last];
            process->mmap_physical_pages[i] = process->mmap_physical_pages[last];
            process->mmap_page_flags[i] = process->mmap_page_flags[last];
            process->mmap_protections[i] = process->mmap_protections[last];
            process->mmap_page_cow[i] = process->mmap_page_cow[last];
            process->mmap_backings[i] = process->mmap_backings[last];
            process->mmap_backing_offsets[i] =
                process->mmap_backing_offsets[last];
        }
        process->mmap_pages[last] = 0;
        process->mmap_physical_pages[last] = 0;
        process->mmap_page_flags[last] = 0;
        process->mmap_protections[last] = 0;
        process->mmap_page_cow[last] = 0;
        process->mmap_backings[last] = 0;
        process->mmap_backing_offsets[last] = 0;
    }
    return 0;
}

int64_t xscore_process_mprotect(uint64_t address, uint64_t length,
                                uint64_t protection) {
    struct xscore_process *process = xscore_process_current();
    if (!process || length == 0 || (address & (PAGE_SIZE - 1)) != 0 ||
        (protection & ~(uint64_t)(AKAOS_PROT_READ | AKAOS_PROT_WRITE |
                                  AKAOS_PROT_EXEC)) != 0 ||
        length > UINT64_MAX - (PAGE_SIZE - 1)) return -AKAOS_EINVAL;
    uint64_t rounded_length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (address > UINT64_MAX - rounded_length) return -AKAOS_EINVAL;
    uint64_t end = address + rounded_length;

    for (uint64_t page = address; page < end; page += PAGE_SIZE) {
        if (!mmap_page_is_reserved(process, page)) return -AKAOS_ENOMEM;
    }

    if (protection & AKAOS_PROT_WRITE) {
        for (uint32_t i = 0; i < process->mmap_page_count; ++i) {
            uint64_t page = process->mmap_pages[i];
            struct process_mmap_backing *backing =
                process->mmap_backings[i];
            if (page < address || page >= end || !backing) continue;
            if (backing->flags & MMAP_BACKING_IMMUTABLE_FILE)
                return -AKAOS_EACCES;
            if ((backing->flags & MMAP_BACKING_MEMFD) == 0U) continue;
            uint32_t seals = __atomic_load_n(&backing->seals,
                                             __ATOMIC_ACQUIRE);
            if (seals & AKAOS_F_SEAL_WRITE) return -AKAOS_EPERM;
            if ((seals & AKAOS_F_SEAL_FUTURE_WRITE) &&
                !(process->mmap_protections[i] & AKAOS_PROT_WRITE))
                return -AKAOS_EPERM;
        }
    }

    uint64_t requested_flags = XSCORE_PAGE_USER;
    if (protection & AKAOS_PROT_WRITE) requested_flags |= XSCORE_PAGE_WRITABLE;
    if (!(protection & AKAOS_PROT_EXEC)) requested_flags |= XSCORE_PAGE_NO_EXECUTE;

    for (uint32_t i = 0; i < process->mmap_page_count; ++i) {
        uint64_t page = process->mmap_pages[i];
        if (page < address || page >= end) continue;
        uint8_t old_protection = process->mmap_protections[i];
        uint64_t physical = process->mmap_physical_pages[i];
        uint64_t actual_flags = requested_flags;
        int is_shared = process->mmap_backings[i] &&
            (process->mmap_backings[i]->flags & MMAP_BACKING_SHARED);
        if (!is_shared && process->mmap_page_cow[i]) {
            actual_flags &= ~XSCORE_PAGE_WRITABLE;
        }
        if (physical) {
            if (protection == AKAOS_PROT_NONE) {
                if (old_protection != AKAOS_PROT_NONE &&
                    !xscore_vmm_unmap_page_in(process->address_space_root,
                                              page, 0)) return -AKAOS_EINVAL;
            } else if (old_protection == AKAOS_PROT_NONE) {
                if (!xscore_vmm_map_page_in(process->address_space_root,
                                            page, physical, actual_flags)) {
                    return -AKAOS_ENOMEM;
                }
            } else if (!process_remap_page(process, page, physical,
                                           actual_flags)) {
                return -AKAOS_ENOMEM;
            }
        }
        process->mmap_page_flags[i] = requested_flags;
        process->mmap_protections[i] = (uint8_t)protection;
    }
    if (protection & AKAOS_PROT_EXEC) process_sync_instruction_cache();
    return 0;
}

uint64_t xscore_process_brk(struct xscore_process *process, uint64_t new_brk) {
    if (!process) return 0;
    if (new_brk == 0 || new_brk == process->heap_break) {
        return process->heap_break;
    }
    if (new_brk < process->heap_start) return process->heap_break;

    uint64_t max_allowed_heap = process->heap_start + (uint64_t)USER_MAX_HEAP_PAGES * PAGE_SIZE;
    if (new_brk > max_allowed_heap) return process->heap_break;

    uint64_t current_reserved_top = process->heap_start +
                                    (uint64_t)process->heap_page_count * PAGE_SIZE;

    if (new_brk > current_reserved_top) {
        uint64_t needed_pages = (new_brk - process->heap_start + PAGE_SIZE - 1) / PAGE_SIZE;
        if (needed_pages > USER_MAX_HEAP_PAGES) return process->heap_break;
        for (uint64_t p = process->heap_page_count; p < needed_pages; ++p) {
            process->heap_physical_pages[p] = 0;
            process->heap_page_cow[p] = 0;
        }
        process->heap_page_count = (uint32_t)needed_pages;
    } else if (new_brk < process->heap_break) {
        uint64_t keep_pages = (new_brk - process->heap_start + PAGE_SIZE - 1) / PAGE_SIZE;
        while (process->heap_page_count > keep_pages) {
            uint32_t last = process->heap_page_count - 1;
            if (process->heap_physical_pages[last]) {
                (void)xscore_vmm_unmap_page_in(process->address_space_root,
                                               process->heap_start +
                                                   (uint64_t)last * PAGE_SIZE,
                                               0);
                (void)xscore_pmm_refcount_dec(
                    process->heap_physical_pages[last]);
            }
            process->heap_physical_pages[last] = 0;
            process->heap_page_cow[last] = 0;
            process->heap_page_count = last;
        }
    }

    process->heap_break = new_brk;
    return process->heap_break;
}
