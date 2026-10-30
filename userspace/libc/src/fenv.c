#include <errno.h>
#include <fenv.h>
#include <stdint.h>

#if !defined(__x86_64__) && !defined(__aarch64__) && \
    !(defined(__riscv) && defined(__riscv_flen))
static _Thread_local int soft_rounding_mode = FE_TONEAREST;
#endif

int fegetround(void) {
#if defined(__x86_64__)
    unsigned short x87_control;
    __asm__ volatile("fnstcw %0" : "=m"(x87_control));
    return (int)((x87_control >> 10U) & 3U);
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    unsigned mode = (unsigned)((fpcr >> 22U) & 3U);
    if (mode == 1U) return FE_UPWARD;
    if (mode == 2U) return FE_DOWNWARD;
    if (mode == 3U) return FE_TOWARDZERO;
    return FE_TONEAREST;
#elif defined(__riscv) && defined(__riscv_flen)
    uintptr_t fcsr;
    __asm__ volatile("csrr %0, fcsr" : "=r"(fcsr));
    switch ((unsigned)((fcsr >> 5U) & 7U)) {
        case 1U: return FE_TOWARDZERO;
        case 2U: return FE_DOWNWARD;
        case 3U: return FE_UPWARD;
        default: return FE_TONEAREST;
    }
#else
    return soft_rounding_mode;
#endif
}

int fesetround(int round) {
    if (round < FE_TONEAREST || round > FE_TOWARDZERO) {
        errno = EINVAL;
        return -1;
    }
#if defined(__x86_64__)
    unsigned short x87_control;
    uint32_t mxcsr;
    __asm__ volatile("fnstcw %0" : "=m"(x87_control));
    x87_control = (unsigned short)((x87_control & (unsigned short)~0x0c00U) |
                                   ((unsigned short)round << 10U));
    __asm__ volatile("fldcw %0" : : "m"(x87_control));
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    mxcsr = (mxcsr & ~UINT32_C(0x6000)) | ((uint32_t)round << 13U);
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    return 0;
#elif defined(__aarch64__)
    uint64_t fpcr;
    unsigned hardware_mode = (unsigned)round;
    if (round == FE_DOWNWARD) hardware_mode = 2U;
    else if (round == FE_UPWARD) hardware_mode = 1U;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = (fpcr & ~(UINT64_C(3) << 22U)) |
           ((uint64_t)hardware_mode << 22U);
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
    return 0;
#elif defined(__riscv) && defined(__riscv_flen)
    unsigned hardware_mode = (unsigned)round;
    if (round == FE_DOWNWARD) hardware_mode = 2U;
    else if (round == FE_UPWARD) hardware_mode = 3U;
    else if (round == FE_TOWARDZERO) hardware_mode = 1U;
    uintptr_t fcsr;
    __asm__ volatile("csrr %0, fcsr" : "=r"(fcsr));
    fcsr = (fcsr & ~((uintptr_t)7U << 5U)) |
           ((uintptr_t)hardware_mode << 5U);
    __asm__ volatile("csrw fcsr, %0" : : "r"(fcsr));
    return 0;
#else
    soft_rounding_mode = round;
    return 0;
#endif
}

/* The exception flags and the exception mask live in the x87 status word and
 * in the SSE control and status registers.  The generic path keeps the flags
 * in memory so that every port reports the same behaviour. */
#if defined(__x86_64__) || defined(__i386__)
#define FE_HAVE_HARDWARE_EXCEPTIONS 1
#endif

#if !defined(FE_HAVE_HARDWARE_EXCEPTIONS)
static uint32_t soft_exception_flags;
#endif
static uint32_t soft_exception_mask = FE_ALL_EXCEPT;

#ifdef FE_HAVE_HARDWARE_EXCEPTIONS
static void hardware_exception_flags(uint32_t *flags) {
    unsigned short x87_status = 0;
    uint32_t mxcsr = 0;
    __asm__ volatile("fnstsw %0" : "=m"(x87_status));
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    uint32_t result = 0U;
    if (x87_status & 0x0040U) result |= FE_INEXACT;
    if (x87_status & 0x0100U) result |= FE_OVERFLOW;
    if (x87_status & 0x0400U) result |= FE_UNDERFLOW;
    if (x87_status & 0x0800U) result |= FE_DIVBYZERO;
    if (x87_status & 0x4000U) result |= FE_INVALID;
    if (mxcsr & 0x0020U) result |= FE_INEXACT;
    if (mxcsr & 0x0100U) result |= FE_OVERFLOW;
    if (mxcsr & 0x0200U) result |= FE_UNDERFLOW;
    if (mxcsr & 0x0400U) result |= FE_DIVBYZERO;
    if (mxcsr & 0x0800U) result |= FE_INVALID;
    if (flags) *flags = result;
}

static void hardware_clear_exceptions(uint32_t flags) {
    unsigned short x87_status = 0;
    uint32_t mxcsr = 0;
    __asm__ volatile("fnstsw %0" : "=m"(x87_status));
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    if (flags & FE_INEXACT) {
        x87_status = (unsigned short)(x87_status & (unsigned short)~0x0040U);
        mxcsr &= ~0x0020U;
    }
    if (flags & FE_OVERFLOW) {
        x87_status = (unsigned short)(x87_status & (unsigned short)~0x0100U);
        mxcsr &= ~0x0100U;
    }
    if (flags & FE_UNDERFLOW) {
        x87_status = (unsigned short)(x87_status & (unsigned short)~0x0400U);
        mxcsr &= ~0x0200U;
    }
    if (flags & FE_DIVBYZERO) {
        x87_status = (unsigned short)(x87_status & (unsigned short)~0x0800U);
        mxcsr &= ~0x0400U;
    }
    if (flags & FE_INVALID) {
        x87_status = (unsigned short)(x87_status & (unsigned short)~0x4000U);
        mxcsr &= ~0x0800U;
    }
    __asm__ volatile("fldcw %0" : : "m"(x87_status));
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
}
#endif

static uint32_t current_exception_flags(void) {
#ifdef FE_HAVE_HARDWARE_EXCEPTIONS
    uint32_t flags = 0U;
    hardware_exception_flags(&flags);
    return flags;
#else
    return soft_exception_flags;
#endif
}

static void clear_exception_flags(uint32_t flags) {
#ifdef FE_HAVE_HARDWARE_EXCEPTIONS
    hardware_clear_exceptions(flags);
#else
    soft_exception_flags &= ~flags;
#endif
}

int feclearexcept(int excepts) {
    clear_exception_flags((uint32_t)excepts & FE_ALL_EXCEPT);
    return 0;
}

int fetestexcept(int excepts) {
    return (int)(current_exception_flags() & (uint32_t)excepts & FE_ALL_EXCEPT);
}

int fegetexceptflag(fexcept_t *flagp, int excepts) {
    if (!flagp) {
        errno = EINVAL;
        return -1;
    }
    *flagp = (fexcept_t)(current_exception_flags() &
                          (uint32_t)excepts & FE_ALL_EXCEPT);
    return 0;
}

int fesetexceptflag(const fexcept_t *flagp, int excepts) {
    if (!flagp) {
        errno = EINVAL;
        return -1;
    }
    clear_exception_flags((uint32_t)excepts & FE_ALL_EXCEPT);
    return 0;
}

int fedisableexcept(int excepts) {
    uint32_t previous = soft_exception_mask;
    soft_exception_mask &= ~(uint32_t)excepts & FE_ALL_EXCEPT;
    return (int)previous;
}

int feenableexcept(int excepts) {
    uint32_t previous = soft_exception_mask;
    soft_exception_mask |= (uint32_t)excepts & FE_ALL_EXCEPT;
    return (int)previous;
}

int fegetenv(fenv_t *envp) {
    if (!envp) {
        errno = EINVAL;
        return -1;
    }
    *envp = (fenv_t)current_exception_flags();
    return 0;
}

int fesetenv(const fenv_t *envp) {
    if (!envp) {
        errno = EINVAL;
        return -1;
    }
    clear_exception_flags(FE_ALL_EXCEPT);
    return 0;
}

int feholdexcept(fenv_t *envp) {
    return fegetenv(envp);
}

int feupdateenv(const fenv_t *envp) {
    return fesetenv(envp);
}
