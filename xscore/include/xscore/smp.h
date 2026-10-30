#ifndef XSCORE_SMP_H
#define XSCORE_SMP_H

#include <stdint.h>
#include <stddef.h>
#include <xscore/acpi.h>

#define SMP_MAX_CPUS ACPI_MAX_CPUS

/* Freestanding spinlock */
typedef struct {
    volatile uint32_t locked;
} xscore_spinlock_t;

#define XSCORE_SPINLOCK_INIT { .locked = 0 }

static inline void xscore_spinlock_init(xscore_spinlock_t *lock) {
    lock->locked = 0;
}

static inline void xscore_spin_lock(xscore_spinlock_t *lock) {
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield");
#else
        __asm__ volatile("" ::: "memory");
#endif
    }
}

static inline void xscore_spin_unlock(xscore_spinlock_t *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

/* Preserve the caller's interrupt mask, including calls from an ISR. */
static inline uintptr_t xscore_irq_save(void) {
    uintptr_t flags;
#if defined(__x86_64__)
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#elif defined(__i386__)
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("mrs %0, daif; msr daifset, #2" : "=r"(flags) :: "memory");
#elif defined(__arm__)
    __asm__ volatile("mrs %0, cpsr; cpsid i" : "=r"(flags) :: "memory");
#elif defined(__riscv)
    __asm__ volatile("csrrci %0, sstatus, 2" : "=r"(flags) :: "memory");
#endif
    return flags;
}

static inline void xscore_irq_restore(uintptr_t flags) {
#if defined(__x86_64__) || defined(__i386__)
    if (flags & (1U << 9)) __asm__ volatile("sti" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
#elif defined(__arm__)
    if (!(flags & (1U << 7))) __asm__ volatile("cpsie i" ::: "memory");
#elif defined(__riscv)
    if (flags & 2) __asm__ volatile("csrsi sstatus, 2" ::: "memory");
#endif
}

static inline uintptr_t xscore_spin_lock_irqsave(xscore_spinlock_t *lock) {
    uintptr_t flags = xscore_irq_save();
    xscore_spin_lock(lock);
    return flags;
}

static inline void xscore_spin_unlock_irqrestore(xscore_spinlock_t *lock, uintptr_t flags) {
    xscore_spin_unlock(lock);
    xscore_irq_restore(flags);
}

struct xscore_cpu_core {
    uint32_t cpu_index;          /* 0 = BSP, 1 = AP 1, etc. */
    uint32_t apic_id;            /* Local APIC ID */
    volatile uint32_t online;    /* 1 if core successfully booted */
    uint64_t kernel_stack_top;   /* Base kernel stack for this core */
    uint64_t ist1_top;           /* Double fault IST stack */
    uint64_t ist2_top;           /* NMI IST stack */
    uint64_t syscall_stack_top;  /* Syscall IST/privilege stack */
    uint16_t tss_selector;       /* TSS GDT selector for this core */
    uint64_t timer_ticks;        /* Timer ticks on this core */
    uint32_t current_thread;     /* Thread index currently running */
};

/* SMP subsystem lifecycle */
#if defined(__x86_64__)
void xscore_smp_initialize(void);
uint32_t xscore_smp_cpu_count(void);
uint32_t xscore_smp_bsp_id(void);
int xscore_smp_is_bsp(void);
uint32_t xscore_smp_current_cpu_index(void);
const struct xscore_cpu_core *xscore_smp_get_core(uint32_t cpu_index);

/* Application Processor entry point (called by trampoline) */
void xscore_smp_ap_entry(void);
int xscore_smp_self_test(void);
int xscore_smp_scheduler_self_test(void);
/* Run bounded Ring 3 workloads on every AP and reap them on the BSP. */
int xscore_smp_userspace_self_test(void);
/* Send a reschedule IPI to an online CPU after a remote wakeup. */
void xscore_smp_request_reschedule(uint32_t cpu_index);
/* Invalidate a page on CPUs currently using root_address before a mapping
 * mutation becomes visible to another address-space owner. */
void xscore_smp_tlb_shootdown(uint64_t root_address, uint64_t virtual_address);
/* Publish the address space currently loaded by this CPU. */
void xscore_smp_note_address_space(uint64_t root_address);
/* Dispatch the fixed-vector TLB IPI on the receiving CPU. */
void xscore_smp_tlb_shootdown_handler(void);
#elif defined(__i386__)
/* x86_32 currently boots a single protected-mode CPU.  Keep the x86 API
 * surface available without pulling in the x86_64 APIC/SMP implementation. */
static inline uint32_t xscore_smp_cpu_count(void) { return 1; }
static inline uint32_t xscore_smp_current_cpu_index(void) { return 0; }
static inline int xscore_smp_is_bsp(void) { return 1; }
static inline void xscore_smp_request_reschedule(uint32_t cpu_index) {
    (void)cpu_index;
}
static inline void xscore_smp_tlb_shootdown(uint64_t root_address,
                                            uint64_t virtual_address) {
    (void)root_address;
    (void)virtual_address;
}
static inline void xscore_smp_note_address_space(uint64_t root_address) {
    (void)root_address;
}
static inline void xscore_smp_tlb_shootdown_handler(void) {}
#else
/* ARM/RISC-V expose real online secondary indices.  Their user-process
 * placement gate remains off, but the scheduler can now admit explicitly
 * targeted kernel work to a registered per-CPU READY mask. */
uint32_t xscore_secondary_online_count(void);
uint32_t xscore_secondary_current_cpu_index(void);
static inline uint32_t xscore_smp_cpu_count(void) {
    uint32_t online = xscore_secondary_online_count();
    uint32_t count = online + 1U; /* include the BSP */
    return count > SMP_MAX_CPUS ? SMP_MAX_CPUS : count;
}
static inline uint32_t xscore_smp_current_cpu_index(void) {
    return xscore_secondary_current_cpu_index();
}
static inline int xscore_smp_is_bsp(void) {
    return xscore_secondary_current_cpu_index() == 0;
}
static inline void xscore_smp_request_reschedule(uint32_t cpu_index) {
    (void)cpu_index;
}
static inline void xscore_smp_tlb_shootdown(uint64_t root_address,
                                            uint64_t virtual_address) {
    (void)root_address;
    (void)virtual_address;
}
static inline void xscore_smp_note_address_space(uint64_t root_address) {
    (void)root_address;
}
static inline void xscore_smp_tlb_shootdown_handler(void) {}
#endif

#endif /* XSCORE_SMP_H */
