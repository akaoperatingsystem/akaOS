#include <xscore/time.h>
#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)
static int leap_year(uint32_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint64_t calendar_to_epoch(uint32_t year, uint32_t month, uint32_t day,
                                  uint32_t hour, uint32_t minute,
                                  uint32_t second) {
    static const uint16_t days_before_month[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    if (year < 1970U || month < 1U || month > 12U || day < 1U || day > 31U ||
        hour > 23U || minute > 59U || second > 60U) return 0;
    uint64_t days = 0;
    for (uint32_t current = 1970U; current < year; ++current)
        days += leap_year(current) ? 366U : 365U;
    days += days_before_month[month] + day - 1U;
    if (month > 2U && leap_year(year)) ++days;
    return days * UINT64_C(86400) + hour * UINT64_C(3600) +
           minute * UINT64_C(60) + second;
}

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t index) {
    out8(UINT16_C(0x70), (uint8_t)(index | UINT8_C(0x80)));
    return in8(UINT16_C(0x71));
}

static uint32_t bcd_to_binary(uint8_t value) {
    return (uint32_t)(value & 0x0fU) + (uint32_t)(value >> 4) * 10U;
}

uint64_t xscore_time_epoch_seconds(void) {
    for (unsigned wait = 0; wait < 100000U; ++wait)
        if ((cmos_read(0x0a) & 0x80U) == 0) break;

    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t status_b = cmos_read(0x0b);
    uint8_t second_check = cmos_read(0x00);
    if (second_check != second) {
        second = second_check;
        minute = cmos_read(0x02);
        hour = cmos_read(0x04);
        day = cmos_read(0x07);
        month = cmos_read(0x08);
        year = cmos_read(0x09);
    }

    int pm = (hour & 0x80U) != 0;
    hour &= 0x7fU;
    if ((status_b & 0x04U) == 0) {
        second = (uint8_t)bcd_to_binary(second);
        minute = (uint8_t)bcd_to_binary(minute);
        hour = (uint8_t)bcd_to_binary(hour);
        day = (uint8_t)bcd_to_binary(day);
        month = (uint8_t)bcd_to_binary(month);
        year = (uint8_t)bcd_to_binary(year);
    }
    if ((status_b & 0x02U) == 0) {
        if (pm && hour < 12U) hour = (uint8_t)(hour + 12U);
        if (!pm && hour == 12U) hour = 0;
    }
    uint64_t epoch = calendar_to_epoch(2000U + year, month, day, hour, minute, second);
    out8(UINT16_C(0x70), 0); /* Re-enable NMI after the CMOS transaction. */
    return epoch;
}
#elif defined(__aarch64__) || defined(__arm__)
uint64_t xscore_time_epoch_seconds(void) {
    /* ARM virt exposes a PrimeCell PL031. DR is a UTC epoch counter. */
    volatile const uint32_t *rtc = (volatile const uint32_t *)(uintptr_t)UINT32_C(0x09010000);
    return (uint64_t)*rtc;
}
#elif defined(__riscv)
static uint64_t divide_u64_u32(uint64_t dividend, uint32_t divisor) {
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((dividend >> bit) & UINT64_C(1));
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= UINT64_C(1) << bit;
        }
    }
    return quotient;
}

uint64_t xscore_time_epoch_seconds(void) {
    /* RISC-V virt exposes QEMU's Goldfish RTC. TIME_LOW/HIGH form a
     * nanosecond UTC counter; read the high half consistently. */
    volatile const uint32_t *rtc =
        (volatile const uint32_t *)(uintptr_t)UINT32_C(0x00101000);
    uint32_t high_before;
    uint32_t low;
    uint32_t high_after;
    do {
        high_before = rtc[1];
        low = rtc[0];
        high_after = rtc[1];
    } while (high_before != high_after);
    uint64_t nanoseconds = ((uint64_t)high_after << 32) | low;
    return divide_u64_u32(nanoseconds, UINT32_C(1000000000));
}
#else
uint64_t xscore_time_epoch_seconds(void) { return 0; }
#endif
