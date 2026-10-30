#include <xscore/random.h>
#include <akaos/errno.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)
static int x86_rdrand_available(void) {
    uint32_t eax = 0U, ebx = 0U, ecx = 0U, edx = 0U;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0U), "c"(0U));
    if (eax < 1U) return 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1U), "c"(0U));
    return (ecx & (UINT32_C(1) << 30U)) != 0U;
}
#endif

#if defined(__x86_64__)
static int x86_rdrand64(uint64_t *value) {
    for (unsigned attempt = 0; attempt < 10U; ++attempt) {
        unsigned char ready = 0U;
        uint64_t sample = 0U;
        __asm__ volatile("rdrand %0; setc %1"
                         : "=r"(sample), "=qm"(ready));
        if (ready) {
            *value = sample;
            return 1;
        }
    }
    return 0;
}
#elif defined(__i386__)
static int x86_rdrand32(uint32_t *value) {
    for (unsigned attempt = 0; attempt < 10U; ++attempt) {
        unsigned char ready = 0U;
        uint32_t sample = 0U;
        __asm__ volatile("rdrand %0; setc %1"
                         : "=r"(sample), "=qm"(ready));
        if (ready) {
            *value = sample;
            return 1;
        }
    }
    return 0;
}
#endif

int64_t xscore_random_read(void *buffer, size_t length) {
    if (length == 0U) return 0;
    if (!buffer) return -AKAOS_EFAULT;
    if (length > (size_t)INT64_MAX) return -AKAOS_EINVAL;
#if defined(__x86_64__)
    if (!x86_rdrand_available()) return -AKAOS_ENOSYS;
    uint8_t *destination = (uint8_t *)buffer;
    size_t offset = 0U;
    while (offset < length) {
        uint64_t sample;
        if (!x86_rdrand64(&sample))
            return offset != 0U ? (int64_t)offset : -AKAOS_EAGAIN;
        size_t count = length - offset;
        if (count > sizeof(sample)) count = sizeof(sample);
        const uint8_t *bytes = (const uint8_t *)&sample;
        for (size_t index = 0U; index < count; ++index)
            destination[offset + index] = bytes[index];
        offset += count;
    }
    return (int64_t)offset;
#elif defined(__i386__)
    if (!x86_rdrand_available()) return -AKAOS_ENOSYS;
    uint8_t *destination = (uint8_t *)buffer;
    size_t offset = 0U;
    while (offset < length) {
        uint32_t sample;
        if (!x86_rdrand32(&sample))
            return offset != 0U ? (int64_t)offset : -AKAOS_EAGAIN;
        size_t count = length - offset;
        if (count > sizeof(sample)) count = sizeof(sample);
        const uint8_t *bytes = (const uint8_t *)&sample;
        for (size_t index = 0U; index < count; ++index)
            destination[offset + index] = bytes[index];
        offset += count;
    }
    return (int64_t)offset;
#else
    (void)buffer;
    (void)length;
    return -AKAOS_ENOSYS;
#endif
}
