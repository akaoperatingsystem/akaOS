#define AKAOS_LIBC_STDIO_IMPLEMENTATION 1
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <akaos/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/wait.h>

struct __akaos_FILE {
    int descriptor;
    unsigned int flags;
    unsigned char pushback_bytes[4];
    unsigned int pushback_length;
    unsigned int pushback_offset;
    int wide_orientation;
    pthread_mutex_t stream_lock;
    volatile uint32_t stream_lock_state;
    char **memstream_buffer;
    size_t *memstream_size;
    char *memstream_snapshot;
};

enum {
    AKAOS_FILE_OWNED = 1U << 0,
    AKAOS_FILE_EOF = 1U << 1,
    AKAOS_FILE_ERROR = 1U << 2,
    AKAOS_FILE_PUSHBACK = 1U << 3,
    AKAOS_FILE_MEMSTREAM = 1U << 4
};

FILE __akaos_stdin = {
    .descriptor = STDIN_FILENO,
    .pushback_bytes = {0, 0, 0, 0},
};
FILE __akaos_stdout = {
    .descriptor = STDOUT_FILENO,
    .pushback_bytes = {0, 0, 0, 0},
};
FILE __akaos_stderr = {
    .descriptor = STDERR_FILENO,
    .pushback_bytes = {0, 0, 0, 0},
};
#undef stdin
#undef stdout
#undef stderr
FILE *stdin = &__akaos_stdin;
FILE *stdout = &__akaos_stdout;
FILE *stderr = &__akaos_stderr;

static int stream_is_valid(FILE *stream) {
    if (!stream || stream->descriptor < 0) {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

static int stream_lock_prepare(FILE *stream) {
    uint32_t state = __atomic_load_n(&stream->stream_lock_state,
                                     __ATOMIC_ACQUIRE);
    if (state == 2U) return 1;
    if (state == 3U) {
        errno = EAGAIN;
        return 0;
    }

    uint32_t expected = 0U;
    if (__atomic_compare_exchange_n(&stream->stream_lock_state, &expected, 1U,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        pthread_mutexattr_t attributes;
        int result = pthread_mutexattr_init(&attributes);
        int attributes_ready = result == 0;
        if (result == 0)
            result = pthread_mutexattr_settype(&attributes,
                                              PTHREAD_MUTEX_RECURSIVE);
        if (result == 0)
            result = pthread_mutex_init(&stream->stream_lock, &attributes);
        if (attributes_ready && pthread_mutexattr_destroy(&attributes) != 0 &&
            result == 0)
            result = EINVAL;
        __atomic_store_n(&stream->stream_lock_state,
                         result == 0 ? 2U : 3U, __ATOMIC_RELEASE);
        if (result != 0) {
            errno = result;
            return 0;
        }
        return 1;
    }

    do {
        (void)sched_yield();
        state = __atomic_load_n(&stream->stream_lock_state, __ATOMIC_ACQUIRE);
    } while (state == 1U);
    if (state != 2U) {
        errno = EAGAIN;
        return 0;
    }
    return 1;
}

static int stream_lock_acquire(FILE *stream) {
    if (!stream_is_valid(stream) || !stream_lock_prepare(stream)) return 0;
    int result = pthread_mutex_lock(&stream->stream_lock);
    if (result != 0) {
        errno = result;
        return 0;
    }
    return 1;
}

static void stream_lock_release(FILE *stream) {
    if (!stream ||
        __atomic_load_n(&stream->stream_lock_state, __ATOMIC_ACQUIRE) != 2U)
        return;
    int result = pthread_mutex_unlock(&stream->stream_lock);
    if (result != 0) errno = result;
}

void flockfile(FILE *stream) {
    (void)stream_lock_acquire(stream);
}

int ftrylockfile(FILE *stream) {
    if (!stream_is_valid(stream) || !stream_lock_prepare(stream)) return 1;
    int result = pthread_mutex_trylock(&stream->stream_lock);
    if (result != 0) errno = result;
    return result;
}

void funlockfile(FILE *stream) {
    stream_lock_release(stream);
}

int remove(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (unlink(path) == 0) return 0;
    return rmdir(path);
}

static void out_char(char **buf_ptr, size_t *rem_ptr, size_t *written_ptr, char c) {
    if (*rem_ptr > 1) {
        **buf_ptr = c;
        (*buf_ptr)++;
        (*rem_ptr)--;
    }
    (*written_ptr)++;
}

static void out_str(char **buf_ptr, size_t *rem_ptr, size_t *written_ptr,
                    const char *s, int width, int prec, int left_align, char pad_char) {
    if (!s) s = "(null)";
    size_t len = strlen(s);
    if (prec >= 0 && (size_t)prec < len) {
        len = (size_t)prec;
    }

    int pad = (width > (int)len) ? (width - (int)len) : 0;

    if (!left_align) {
        while (pad-- > 0) {
            out_char(buf_ptr, rem_ptr, written_ptr, pad_char);
        }
    }

    for (size_t i = 0; i < len; ++i) {
        out_char(buf_ptr, rem_ptr, written_ptr, s[i]);
    }

    if (left_align) {
        while (pad-- > 0) {
            out_char(buf_ptr, rem_ptr, written_ptr, ' ');
        }
    }
}

#if defined(__i386__) || defined(__arm__) || (defined(__riscv) && __riscv_xlen == 32)
uint64_t __udivdi3(uint64_t a, uint64_t b) {
    if (b == 0) return 0;
    uint64_t q = 0;
    uint64_t r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) {
            r -= b;
            q |= (UINT64_C(1) << i);
        }
    }
    return q;
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    if (b == 0) return 0;
    uint64_t r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) {
            r -= b;
        }
    }
    return r;
}

int64_t __divdi3(int64_t dividend, int64_t divisor) {
    if (divisor == 0) return 0;
    int negative = (dividend < 0) != (divisor < 0);
    uint64_t numerator = dividend < 0 ? UINT64_C(0) - (uint64_t)dividend :
                                        (uint64_t)dividend;
    uint64_t denominator = divisor < 0 ? UINT64_C(0) - (uint64_t)divisor :
                                         (uint64_t)divisor;
    uint64_t quotient = __udivdi3(numerator, denominator);
    return (int64_t)(negative ? UINT64_C(0) - quotient : quotient);
}

int64_t __moddi3(int64_t dividend, int64_t divisor) {
    if (divisor == 0) return 0;
    uint64_t numerator = dividend < 0 ? UINT64_C(0) - (uint64_t)dividend :
                                        (uint64_t)dividend;
    uint64_t denominator = divisor < 0 ? UINT64_C(0) - (uint64_t)divisor :
                                         (uint64_t)divisor;
    uint64_t remainder = __umoddi3(numerator, denominator);
    return (int64_t)(dividend < 0 ? UINT64_C(0) - remainder : remainder);
}
#endif

#if defined(__arm__)
unsigned int __aeabi_uidiv(unsigned int n, unsigned int d) {
    if (d == 0) return 0;
    unsigned int q = 0;
    unsigned int r = 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (1U << i);
        }
    }
    return q;
}

int __aeabi_idiv(int n, int d) {
    int neg = 0;
    if (n < 0) { n = -n; neg = !neg; }
    if (d < 0) { d = -d; neg = !neg; }
    unsigned int q = __aeabi_uidiv((unsigned int)n, (unsigned int)d);
    return neg ? -(int)q : (int)q;
}

typedef struct { unsigned int quot; unsigned int rem; } __uidivmod_result;
__uidivmod_result __aeabi_uidivmod(unsigned int n, unsigned int d) {
    __uidivmod_result res;
    if (d == 0) { res.quot = 0; res.rem = 0; return res; }
    unsigned int q = 0;
    unsigned int r = 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (1U << i);
        }
    }
    res.quot = q;
    res.rem = r;
    return res;
}

typedef struct { int quot; int rem; } __idivmod_result;
__idivmod_result __aeabi_idivmod(int n, int d) {
    __idivmod_result res;
    int neg_q = 0;
    int neg_r = 0;
    if (n < 0) { n = -n; neg_q = !neg_q; neg_r = 1; }
    if (d < 0) { d = -d; neg_q = !neg_q; }
    __uidivmod_result ures = __aeabi_uidivmod((unsigned int)n, (unsigned int)d);
    res.quot = neg_q ? -(int)ures.quot : (int)ures.quot;
    res.rem = neg_r ? -(int)ures.rem : (int)ures.rem;
    return res;
}
#endif

static uint64_t udivmod64(uint64_t n, uint32_t d, uint32_t *rem) {
#if defined(__x86_64__)
    if (rem) *rem = (uint32_t)(n % d);
    return n / d;
#else
    if (n <= UINT32_MAX) {
        uint32_t n32 = (uint32_t)n;
        if (rem) *rem = n32 % d;
        return (uint64_t)(n32 / d);
    }
    uint64_t q = 0;
    uint64_t r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (UINT64_C(1) << i);
        }
    }
    if (rem) *rem = (uint32_t)r;
    return q;
#endif
}

static void out_num(char **buf_ptr, size_t *rem_ptr, size_t *written_ptr,
                    uint64_t val, int is_signed, int base, int uppercase,
                    int width, int prec, int left_align, char pad_char,
                    int show_sign, int space_sign, int alt_form) {
    char digits[65];
    int count = 0;
    const char *hex_digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    int negative = 0;
    if (is_signed && (int64_t)val < 0) {
        negative = 1;
        val = (uint64_t)(-(int64_t)val);
    }

    if (val == 0) {
        if (prec != 0) {
            digits[count++] = '0';
        }
    } else {
        while (val > 0) {
            uint32_t rem = 0;
            val = udivmod64(val, (uint32_t)base, &rem);
            digits[count++] = hex_digits[rem];
        }
    }

    int num_len = count;
    int zeros = (prec > num_len) ? (prec - num_len) : 0;
    int prefix_len = 0;
    char prefix[3];
    prefix[0] = '\0';

    if (negative) {
        prefix[prefix_len++] = '-';
    } else if (show_sign) {
        prefix[prefix_len++] = '+';
    } else if (space_sign) {
        prefix[prefix_len++] = ' ';
    }

    if (alt_form && base == 16 && count > 0) {
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = uppercase ? 'X' : 'x';
    } else if (alt_form && base == 8 && zeros == 0 && count > 0) {
        prefix[prefix_len++] = '0';
    }
    prefix[prefix_len] = '\0';

    int total_len = prefix_len + zeros + num_len;
    int pad = (width > total_len) ? (width - total_len) : 0;

    if (pad_char == '0' && !left_align && prec < 0) {
        /* Prefix first, then zero padding */
        for (int i = 0; i < prefix_len; ++i) {
            out_char(buf_ptr, rem_ptr, written_ptr, prefix[i]);
        }
        while (pad-- > 0) {
            out_char(buf_ptr, rem_ptr, written_ptr, '0');
        }
    } else {
        if (!left_align) {
            while (pad-- > 0) {
                out_char(buf_ptr, rem_ptr, written_ptr, ' ');
            }
        }
        for (int i = 0; i < prefix_len; ++i) {
            out_char(buf_ptr, rem_ptr, written_ptr, prefix[i]);
        }
    }

    while (zeros-- > 0) {
        out_char(buf_ptr, rem_ptr, written_ptr, '0');
    }

    while (count > 0) {
        out_char(buf_ptr, rem_ptr, written_ptr, digits[--count]);
    }

    if (left_align) {
        while (pad-- > 0) {
            out_char(buf_ptr, rem_ptr, written_ptr, ' ');
        }
    }
}

static size_t format_fixed_float(long double value, unsigned int precision,
                                int alternate, char *output,
                                uint64_t *integer_out) {
    if (value < 0.0L || value >= (long double)UINT64_MAX) return 0U;
    uint64_t integer = (uint64_t)value;
    long double fraction = value - (long double)integer;
    uint64_t scale = 1U;
    for (unsigned int digit = 0; digit < precision; ++digit)
        scale *= UINT64_C(10);
    uint64_t fractional = (uint64_t)(fraction * (long double)scale + 0.5L);
    if (fractional >= scale && scale != 0U) {
        fractional = 0U;
        if (integer == UINT64_MAX) return 0U;
        ++integer;
    }
    if (integer_out) *integer_out = integer;

    char reversed[24];
    size_t integer_digits = 0U;
    do {
        reversed[integer_digits++] = (char)('0' + integer % 10U);
        integer /= 10U;
    } while (integer != 0U && integer_digits < sizeof(reversed));
    size_t length = 0U;
    while (integer_digits != 0U)
        output[length++] = reversed[--integer_digits];

    if (precision != 0U || alternate) output[length++] = '.';
    if (precision != 0U) {
        uint64_t divisor = scale / 10U;
        for (unsigned int digit = 0; digit < precision; ++digit) {
            uint64_t value_digit = divisor ? fractional / divisor : 0U;
            output[length++] = (char)('0' + value_digit % 10U);
            if (divisor != 0U) {
                fractional %= divisor;
                divisor /= 10U;
            }
        }
    }
    return length;
}

static size_t format_float_value(long double value, char specifier,
                                 int precision, int show_sign,
                                 int space_sign, int alternate,
                                 char *output, size_t capacity) {
    if (precision < 0) precision = 6;
    if (precision > 18) precision = 18;
    if (capacity < 32U) return 0U;
    size_t length = 0U;
    int negative = signbit(value) != 0;
    if (negative) {
        output[length++] = '-';
        value = -value;
    } else if (show_sign) {
        output[length++] = '+';
    } else if (space_sign) {
        output[length++] = ' ';
    }

    if (isnan(value) || isinf(value)) {
        const char *word = isnan(value) ? "nan" : "inf";
        for (size_t index = 0; word[index]; ++index)
            output[length++] = (specifier == 'F' || specifier == 'E' ||
                                specifier == 'G')
                                   ? (char)(word[index] - ('a' - 'A'))
                                   : word[index];
        output[length] = '\0';
        return length;
    }

    int uppercase = specifier == 'E' || specifier == 'G' || specifier == 'F';
    char lower_specifier = uppercase
        ? (char)(specifier - ('A' - 'a')) : specifier;
    int exponent = 0;
    long double normalized = value;
    if (lower_specifier == 'e' || lower_specifier == 'g') {
        if (normalized != 0.0L) {
            while (normalized >= 10.0L && exponent < 5000) {
                normalized /= 10.0L;
                ++exponent;
            }
            while (normalized < 1.0L && exponent > -5000) {
                normalized *= 10.0L;
                --exponent;
            }
        }
    }

    int use_exponential = lower_specifier == 'e';
    unsigned int fractional_precision = (unsigned int)precision;
    if (lower_specifier == 'g') {
        unsigned int significant = precision == 0 ? 1U : (unsigned int)precision;
        use_exponential = value != 0.0L &&
            (exponent < -4 || exponent >= (int)significant);
        fractional_precision = use_exponential ? significant - 1U :
            (exponent < 0 ? significant - 1U + (unsigned int)(-exponent) :
             significant > (unsigned int)(exponent + 1)
                 ? significant - (unsigned int)(exponent + 1) : 0U);
        if (fractional_precision > 18U) fractional_precision = 18U;
    }

    char formatted[128];
    uint64_t rounded_integer = 0U;
    long double fixed_value = use_exponential ? normalized : value;
    if (fixed_value >= (long double)UINT64_MAX) return 0U;
    size_t formatted_length = format_fixed_float(
        fixed_value, fractional_precision, alternate, formatted,
        &rounded_integer);
    if (formatted_length == 0U) return 0U;
    if (use_exponential && rounded_integer >= 10U) {
        ++exponent;
        formatted_length = format_fixed_float(
            1.0L, fractional_precision, alternate, formatted, NULL);
    }

    if (lower_specifier == 'g' && !alternate) {
        while (formatted_length != 0U &&
               formatted[formatted_length - 1U] == '0')
            --formatted_length;
        if (formatted_length != 0U && formatted[formatted_length - 1U] == '.')
            --formatted_length;
    }
    memcpy(output + length, formatted, formatted_length);
    length += formatted_length;

    if (use_exponential) {
        output[length++] = uppercase ? 'E' : 'e';
        output[length++] = exponent < 0 ? '-' : '+';
        unsigned int magnitude = exponent < 0
            ? (unsigned int)(-exponent) : (unsigned int)exponent;
        char reversed_exponent[8];
        size_t exponent_digits = 0U;
        do {
            reversed_exponent[exponent_digits++] =
                (char)('0' + magnitude % 10U);
            magnitude /= 10U;
        } while (magnitude != 0U && exponent_digits < sizeof(reversed_exponent));
        while (exponent_digits < 2U)
            reversed_exponent[exponent_digits++] = '0';
        while (exponent_digits != 0U)
            output[length++] = reversed_exponent[--exponent_digits];
    }
    output[length] = '\0';
    return length;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *buf_ptr = str;
    size_t rem = size;
    size_t written = 0;

    if (!format) return 0;

    while (*format) {
        if (*format != '%') {
            out_char(&buf_ptr, &rem, &written, *format++);
            continue;
        }

        format++; /* Skip '%' */
        if (*format == '\0') break;
        if (*format == '%') {
            out_char(&buf_ptr, &rem, &written, '%');
            format++;
            continue;
        }

        /* Flags */
        int left_align = 0;
        int show_sign = 0;
        int space_sign = 0;
        int alt_form = 0;
        char pad_char = ' ';

        while (*format == '-' || *format == '+' || *format == ' ' || *format == '#' || *format == '0') {
            if (*format == '-') left_align = 1;
            else if (*format == '+') show_sign = 1;
            else if (*format == ' ') space_sign = 1;
            else if (*format == '#') alt_form = 1;
            else if (*format == '0') pad_char = '0';
            format++;
        }
        if (left_align) pad_char = ' ';

        /* Field width */
        int width = 0;
        if (*format == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_align = 1;
                width = -width;
            }
            format++;
        } else {
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
        }

        /* Precision */
        int prec = -1;
        if (*format == '.') {
            format++;
            prec = 0;
            if (*format == '*') {
                prec = va_arg(ap, int);
                format++;
            } else {
                while (*format >= '0' && *format <= '9') {
                    prec = prec * 10 + (*format - '0');
                    format++;
                }
            }
        }

        /* Length modifier */
        int is_long = 0;
        int is_long_long = 0;
        int is_size_t = 0;
        int is_long_double = 0;

        if (*format == 'l') {
            format++;
            if (*format == 'l') {
                is_long_long = 1;
                format++;
            } else {
                is_long = 1;
            }
        } else if (*format == 'z') {
            is_size_t = 1;
            format++;
        } else if (*format == 'h') {
            format++;
            if (*format == 'h') format++;
        } else if (*format == 'L') {
            is_long_double = 1;
            format++;
        }

        char spec = *format++;
        if (spec == 'c') {
            char c = (char)va_arg(ap, int);
            out_char(&buf_ptr, &rem, &written, c);
        } else if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            out_str(&buf_ptr, &rem, &written, s, width, prec, left_align, pad_char);
        } else if (spec == 'd' || spec == 'i') {
            int64_t val;
            if (is_long_long || is_long) val = (int64_t)va_arg(ap, long);
            else if (is_size_t) val = (int64_t)va_arg(ap, ssize_t);
            else val = (int64_t)va_arg(ap, int);
            out_num(&buf_ptr, &rem, &written, (uint64_t)val, 1, 10, 0, width, prec, left_align, pad_char, show_sign, space_sign, alt_form);
        } else if (spec == 'u') {
            uint64_t val;
            if (is_long_long || is_long) val = va_arg(ap, unsigned long);
            else if (is_size_t) val = (uint64_t)va_arg(ap, size_t);
            else val = (uint64_t)va_arg(ap, unsigned int);
            out_num(&buf_ptr, &rem, &written, val, 0, 10, 0, width, prec, left_align, pad_char, 0, 0, 0);
        } else if (spec == 'x') {
            uint64_t val;
            if (is_long_long || is_long) val = va_arg(ap, unsigned long);
            else if (is_size_t) val = (uint64_t)va_arg(ap, size_t);
            else val = (uint64_t)va_arg(ap, unsigned int);
            out_num(&buf_ptr, &rem, &written, val, 0, 16, 0, width, prec, left_align, pad_char, 0, 0, alt_form);
        } else if (spec == 'X') {
            uint64_t val;
            if (is_long_long || is_long) val = va_arg(ap, unsigned long);
            else if (is_size_t) val = (uint64_t)va_arg(ap, size_t);
            else val = (uint64_t)va_arg(ap, unsigned int);
            out_num(&buf_ptr, &rem, &written, val, 0, 16, 1, width, prec, left_align, pad_char, 0, 0, alt_form);
        } else if (spec == 'p') {
            void *ptr = va_arg(ap, void *);
            if (!ptr) {
                out_str(&buf_ptr, &rem, &written, "(nil)", width, prec, left_align, ' ');
            } else {
                out_num(&buf_ptr, &rem, &written, (uint64_t)(uintptr_t)ptr, 0, 16, 0, width, prec, left_align, pad_char, 0, 0, 1);
            }
        } else if (spec == 'b') {
            uint64_t val = (uint64_t)va_arg(ap, unsigned int);
            out_num(&buf_ptr, &rem, &written, val, 0, 2, 0, width, prec, left_align, pad_char, 0, 0, 0);
        } else if (spec == 'f' || spec == 'F' || spec == 'e' ||
                   spec == 'E' || spec == 'g' || spec == 'G') {
            long double val = is_long_double
                ? va_arg(ap, long double)
                : (long double)va_arg(ap, double);
            char formatted[128];
            size_t formatted_length = format_float_value(
                val, spec, prec, show_sign, space_sign, alt_form,
                formatted, sizeof(formatted));
            if (formatted_length == 0U) {
                errno = ERANGE;
                out_str(&buf_ptr, &rem, &written, "inf", width, -1,
                        left_align, pad_char);
            } else {
                out_str(&buf_ptr, &rem, &written, formatted, width, -1,
                        left_align, pad_char);
            }
        } else {
            out_char(&buf_ptr, &rem, &written, spec);
        }
    }

    if (size > 0 && str) {
        if (rem > 0) {
            *buf_ptr = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return (int)written;
}

int vasprintf(char **strp, const char *format, va_list ap) {
    if (!strp || !format) {
        errno = EINVAL;
        return -1;
    }
    va_list measure;
    va_copy(measure, ap);
    int length = vsnprintf(NULL, 0U, format, measure);
    va_end(measure);
    if (length < 0 || (size_t)length == SIZE_MAX) return -1;
    char *buffer = (char *)malloc((size_t)length + 1U);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }
    va_list render;
    va_copy(render, ap);
    int rendered = vsnprintf(buffer, (size_t)length + 1U, format, render);
    va_end(render);
    if (rendered < 0) {
        free(buffer);
        return -1;
    }
    *strp = buffer;
    return rendered;
}

int asprintf(char **strp, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vasprintf(strp, format, ap);
    va_end(ap);
    return result;
}

static int scan_integer_conversion(int specifier) {
    return specifier == 'd' || specifier == 'i' || specifier == 'u' ||
           specifier == 'o' || specifier == 'x' || specifier == 'X' ||
           specifier == 'p';
}

static int scan_float_character(char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '+' ||
           character == '-' || character == '.' || character == '(' ||
           character == ')';
}

static int scan_set_contains(const unsigned char set[256], int character) {
    return set[(unsigned char)character] != 0;
}

int vsscanf(const char *input, const char *format, va_list ap) {
    if (!input || !format) {
        errno = EINVAL;
        return EOF;
    }
    const char *source = input;
    int assignments = 0;
    int conversions = 0;
    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*format)) ++format;
            while (isspace((unsigned char)*source)) ++source;
            continue;
        }
        if (*format != '%') {
            if (*source != *format) return assignments;
            if (*source == '\0') return assignments == 0 ? EOF : assignments;
            ++source;
            ++format;
            continue;
        }
        ++format;
        if (*format == '%') {
            if (*source != '%') return assignments;
            ++source;
            ++format;
            continue;
        }

        int suppress = 0;
        if (*format == '*') { suppress = 1; ++format; }
        size_t width = 0U;
        while (*format >= '0' && *format <= '9') {
            if (width < 1000000U) width = width * 10U + (size_t)(*format - '0');
            ++format;
        }
        if (width == 0U) width = SIZE_MAX;

        enum { SCAN_DEFAULT, SCAN_HH, SCAN_H, SCAN_L, SCAN_LL, SCAN_Z,
               SCAN_T, SCAN_J, SCAN_CAP_L } length = SCAN_DEFAULT;
        if (*format == 'h') {
            ++format;
            if (*format == 'h') { ++format; length = SCAN_HH; }
            else length = SCAN_H;
        } else if (*format == 'l') {
            ++format;
            if (*format == 'l') { ++format; length = SCAN_LL; }
            else length = SCAN_L;
        } else if (*format == 'z') { ++format; length = SCAN_Z; }
        else if (*format == 't') { ++format; length = SCAN_T; }
        else if (*format == 'j') { ++format; length = SCAN_J; }
        else if (*format == 'L') { ++format; length = SCAN_CAP_L; }

        char specifier = *format++;
        if (specifier == '\0') break;
        ++conversions;
        if (specifier == 'n') {
            if (!suppress) {
                size_t consumed = (size_t)(source - input);
                switch (length) {
                    case SCAN_HH: *va_arg(ap, signed char *) = (signed char)consumed; break;
                    case SCAN_H: *va_arg(ap, short *) = (short)consumed; break;
                    case SCAN_L: *va_arg(ap, long *) = (long)consumed; break;
                    case SCAN_LL: *va_arg(ap, long long *) = (long long)consumed; break;
                    case SCAN_Z: *va_arg(ap, size_t *) = consumed; break;
                    case SCAN_T: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)consumed; break;
                    case SCAN_J: *va_arg(ap, intmax_t *) = (intmax_t)consumed; break;
                    default: *va_arg(ap, int *) = (int)consumed; break;
                }
            }
            continue;
        }

        if (specifier == 'c') {
            size_t count = width == SIZE_MAX ? 1U : width;
            size_t available = 0U;
            while (available < count && source[available]) ++available;
            if (available != count) return assignments;
            if (!suppress) {
                char *destination = va_arg(ap, char *);
                for (size_t index = 0U; index < count; ++index)
                    destination[index] = source[index];
                ++assignments;
            }
            source += count;
            continue;
        }

        if (specifier == '[') {
            unsigned char set[256] = {0};
            int invert = 0;
            if (*format == '^') { invert = 1; ++format; }
            if (*format == ']') set[(unsigned char)*format++] = 1U;
            while (*format && *format != ']') {
                unsigned char first = (unsigned char)*format++;
                if (*format == '-' && format[1] && format[1] != ']') {
                    ++format;
                    unsigned char last = (unsigned char)*format++;
                    for (unsigned int value = first; value <= last; ++value)
                        set[value] = 1U;
                } else {
                    set[first] = 1U;
                }
            }
            if (*format != ']') return assignments;
            ++format;
            size_t count = 0U;
            while (source[count] && count < width &&
                   (scan_set_contains(set, source[count]) != invert)) ++count;
            if (count == 0U) return assignments;
            if (!suppress) {
                char *destination = va_arg(ap, char *);
                for (size_t index = 0U; index < count; ++index)
                    destination[index] = source[index];
                destination[count] = '\0';
                ++assignments;
            }
            source += count;
            continue;
        }

        if (specifier != 'c') {
            while (isspace((unsigned char)*source)) ++source;
        }
        if (!*source) return assignments == 0 && conversions == 1 ? EOF : assignments;

        if (specifier == 's') {
            size_t count = 0U;
            while (source[count] && !isspace((unsigned char)source[count]) &&
                   count < width) ++count;
            if (count == 0U) return assignments;
            if (!suppress) {
                char *destination = va_arg(ap, char *);
                for (size_t index = 0U; index < count; ++index)
                    destination[index] = source[index];
                destination[count] = '\0';
                ++assignments;
            }
            source += count;
            continue;
        }

        if (scan_integer_conversion(specifier)) {
            char token[256];
            size_t count = 0U;
            while (source[count] && count < width && count + 1U < sizeof(token)) {
                char c = source[count];
                if (!(scan_float_character(c) || c == '_')) break;
                token[count] = c;
                ++count;
            }
            token[count] = '\0';
            if (count == 0U) return assignments;
            char *parsed_end = NULL;
            int base = specifier == 'o' ? 8 :
                       (specifier == 'x' || specifier == 'X' || specifier == 'p') ? 16 :
                       specifier == 'i' ? 0 : 10;
            int unsigned_conversion = specifier == 'u' || specifier == 'o' ||
                                      specifier == 'x' || specifier == 'X' ||
                                      specifier == 'p';
            long long signed_value = strtoll(token, &parsed_end, base);
            unsigned long long unsigned_value =
                strtoull(token, &parsed_end, base);
            if (parsed_end == token) return assignments;
            size_t consumed = (size_t)(parsed_end - token);
            if (!suppress) {
                if (specifier == 'p') {
                    *va_arg(ap, void **) = (void *)(uintptr_t)unsigned_value;
                } else if (unsigned_conversion) {
                    switch (length) {
                        case SCAN_HH: *va_arg(ap, unsigned char *) = (unsigned char)unsigned_value; break;
                        case SCAN_H: *va_arg(ap, unsigned short *) = (unsigned short)unsigned_value; break;
                        case SCAN_L: *va_arg(ap, unsigned long *) = (unsigned long)unsigned_value; break;
                        case SCAN_LL: *va_arg(ap, unsigned long long *) = unsigned_value; break;
                        case SCAN_Z: *va_arg(ap, size_t *) = (size_t)unsigned_value; break;
                        case SCAN_T: *va_arg(ap, uintptr_t *) = (uintptr_t)unsigned_value; break;
                        case SCAN_J: *va_arg(ap, uintmax_t *) = (uintmax_t)unsigned_value; break;
                        default: *va_arg(ap, unsigned int *) = (unsigned int)unsigned_value; break;
                    }
                } else {
                    switch (length) {
                        case SCAN_HH: *va_arg(ap, signed char *) = (signed char)signed_value; break;
                        case SCAN_H: *va_arg(ap, short *) = (short)signed_value; break;
                        case SCAN_L: *va_arg(ap, long *) = (long)signed_value; break;
                        case SCAN_LL: *va_arg(ap, long long *) = signed_value; break;
                        case SCAN_Z: *va_arg(ap, ssize_t *) = (ssize_t)signed_value; break;
                        case SCAN_T: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)signed_value; break;
                        case SCAN_J: *va_arg(ap, intmax_t *) = (intmax_t)signed_value; break;
                        default: *va_arg(ap, int *) = (int)signed_value; break;
                    }
                }
                ++assignments;
            }
            source += consumed;
            continue;
        }

        if (specifier == 'a' || specifier == 'A' || specifier == 'e' ||
            specifier == 'E' || specifier == 'f' || specifier == 'F' ||
            specifier == 'g' || specifier == 'G') {
            char token[256];
            size_t count = 0U;
            while (source[count] && count < width && count + 1U < sizeof(token) &&
                   scan_float_character(source[count])) {
                token[count] = source[count];
                ++count;
            }
            token[count] = '\0';
            char *parsed_end = NULL;
            long double value = strtold(token, &parsed_end);
            if (parsed_end == token) return assignments;
            size_t consumed = (size_t)(parsed_end - token);
            if (!suppress) {
                if (length == SCAN_CAP_L) *va_arg(ap, long double *) = value;
                else if (length == SCAN_L) *va_arg(ap, double *) = (double)value;
                else *va_arg(ap, float *) = (float)value;
                ++assignments;
            }
            source += consumed;
            continue;
        }
        return assignments;
    }
    return assignments;
}

int sscanf(const char *input, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsscanf(input, format, ap);
    va_end(ap);
    return result;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}

int vswprintf(wchar_t *destination, size_t capacity,
              const wchar_t *format, va_list arguments) {
    if (!format || (!destination && capacity != 0U)) {
        errno = EINVAL;
        return -1;
    }
    char narrow_format[512];
    size_t format_length = 0U;
    while (format[format_length] != L'\0') {
        if (format_length + 1U >= sizeof(narrow_format) ||
            (uint32_t)format[format_length] > 0x7fU) {
            errno = EILSEQ;
            if (destination && capacity != 0U) destination[0] = L'\0';
            return -1;
        }
        narrow_format[format_length] = (char)format[format_length];
        ++format_length;
    }
    narrow_format[format_length] = '\0';

    va_list measure;
    va_copy(measure, arguments);
    int required = vsnprintf(NULL, 0U, narrow_format, measure);
    va_end(measure);
    if (required < 0) return -1;
    if (capacity == 0U) return required;
    if ((size_t)required >= capacity) {
        destination[0] = L'\0';
        return -1;
    }
    char *narrow_output = (char *)malloc((size_t)required + 1U);
    if (!narrow_output) {
        errno = ENOMEM;
        destination[0] = L'\0';
        return -1;
    }
    va_list render;
    va_copy(render, arguments);
    int rendered = vsnprintf(narrow_output, (size_t)required + 1U,
                             narrow_format, render);
    va_end(render);
    if (rendered < 0 || rendered > required) {
        free(narrow_output);
        destination[0] = L'\0';
        return -1;
    }
    for (int index = 0; index < rendered; ++index)
        destination[index] = (wchar_t)(unsigned char)narrow_output[index];
    destination[rendered] = L'\0';
    free(narrow_output);
    return rendered;
}

int swprintf(wchar_t *destination, size_t capacity,
             const wchar_t *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vswprintf(destination, capacity, format, arguments);
    va_end(arguments);
    return result;
}

int vsprintf(char *str, const char *format, va_list ap) {
    if (!str) {
        errno = EFAULT;
        return -1;
    }
    return vsnprintf(str, SIZE_MAX, format, ap);
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, SIZE_MAX, format, ap);
    va_end(ap);
    return res;
}

int vfdprintf(int fd, const char *format, va_list ap) {
    char buf[1024];
    va_list aq;
    va_copy(aq, ap);
    int len = vsnprintf(buf, sizeof(buf), format, aq);
    va_end(aq);

    if (len <= 0) return len;

    if ((size_t)len < sizeof(buf)) {
        if (write(fd, buf, (size_t)len) < 0) return -1;
    } else {
        /* Large output: allocate temporary buffer */
        char *dynamic_buf = (char *)malloc((size_t)len + 1);
        if (dynamic_buf) {
            vsnprintf(dynamic_buf, (size_t)len + 1, format, ap);
            ssize_t written = write(fd, dynamic_buf, (size_t)len);
            free(dynamic_buf);
            if (written < 0 || written != len) return -1;
        } else {
            if (write(fd, buf, sizeof(buf) - 1U) < 0) return -1;
        }
    }
    return len;
}

int fdprintf(int fd, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vfdprintf(fd, format, ap);
    va_end(ap);
    return res;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    if (!stream_lock_acquire(stream)) return -1;
    int result = vfdprintf(stream->descriptor, format, ap);
    if (result < 0) stream->flags |= AKAOS_FILE_ERROR;
    stream_lock_release(stream);
    return result;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vfprintf(stream, format, ap);
    va_end(ap);
    return result;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vfprintf(stdout, format, ap);
    va_end(ap);
    return res;
}

int vprintf(const char *format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

static int parse_open_mode(const char *mode, int *flags) {
    if (!mode || !*mode || !flags) return 0;
    int plus = 0;
    for (const char *cursor = mode + 1; *cursor; ++cursor) {
        if (*cursor == '+') plus = 1;
        else if (*cursor != 'b' && *cursor != 'e' && *cursor != 'x') return 0;
    }
    switch (*mode) {
        case 'r': *flags = plus ? O_RDWR : O_RDONLY; return 1;
        case 'w': *flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; return 1;
        case 'a': *flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; return 1;
        default: return 0;
    }
}

FILE *fdopen(int fd, const char *mode) {
    int ignored_flags;
    if (fd < 0 || !parse_open_mode(mode, &ignored_flags)) {
        errno = EINVAL;
        return NULL;
    }
    FILE *stream = (FILE *)malloc(sizeof(*stream));
    if (!stream) return NULL;
    stream->descriptor = fd;
    stream->flags = AKAOS_FILE_OWNED;
    memset(stream->pushback_bytes, 0, sizeof(stream->pushback_bytes));
    stream->pushback_length = 0U;
    stream->pushback_offset = 0U;
    stream->wide_orientation = 0;
    stream->stream_lock_state = 0U;
    stream->memstream_buffer = NULL;
    stream->memstream_size = NULL;
    stream->memstream_snapshot = NULL;
    return stream;
}

FILE *fopen(const char *path, const char *mode) {
    int flags;
    if (!path || !parse_open_mode(mode, &flags)) {
        errno = EINVAL;
        return NULL;
    }
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    FILE *stream = fdopen(fd, mode);
    if (!stream) (void)close(fd);
    return stream;
}

FILE *fopen64(const char *path, const char *mode) {
    return fopen(path, mode);
}

FILE *freopen64(const char *path, const char *mode, FILE *stream) {
    return freopen(path, mode, stream);
}

FILE *tmpfile(void) {
    static uint32_t next_temporary_id;
    char path[64];
    for (unsigned attempt = 0; attempt < 64U; ++attempt) {
        uint32_t id = __atomic_add_fetch(&next_temporary_id, 1U,
                                         __ATOMIC_RELAXED);
        int length = snprintf(path, sizeof(path), "/tmp/akaos-%u-%u",
                              (unsigned)getpid(), id);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        int descriptor = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC,
                              0600);
        if (descriptor < 0) {
            if (errno == EEXIST) continue;
            return NULL;
        }
        if (unlink(path) != 0) {
            int saved_errno = errno;
            (void)close(descriptor);
            errno = saved_errno;
            return NULL;
        }
        FILE *stream = fdopen(descriptor, "w+b");
        if (!stream) (void)close(descriptor);
        return stream;
    }
    errno = EEXIST;
    return NULL;
}

FILE *open_memstream(char **buffer, size_t *length) {
    if (!buffer || !length) {
        errno = EINVAL;
        return NULL;
    }
    FILE *stream = tmpfile();
    if (!stream) return NULL;
    stream->flags |= AKAOS_FILE_MEMSTREAM;
    stream->memstream_buffer = buffer;
    stream->memstream_size = length;
    *buffer = NULL;
    *length = 0;
    return stream;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    if (!stream) return fopen(path, mode);
    FILE *replacement = fopen(path, mode);
    if (!replacement) return NULL;
    if (stream->flags & AKAOS_FILE_OWNED) (void)close(stream->descriptor);
    *stream = *replacement;
    free(replacement);
    return stream;
}

int fflush(FILE *stream) {
    if (!stream) return 0; /* akaOS streams are unbuffered */
    if (!stream_is_valid(stream)) return EOF;
    if ((stream->flags & AKAOS_FILE_MEMSTREAM) == 0U) return 0;

    struct stat status;
    if (fstat(stream->descriptor, &status) != 0) {
        stream->flags |= AKAOS_FILE_ERROR;
        return EOF;
    }
    if (status.st_size < 0 || (uint64_t)status.st_size >= SIZE_MAX) {
        errno = EOVERFLOW;
        stream->flags |= AKAOS_FILE_ERROR;
        return EOF;
    }
    size_t size = (size_t)status.st_size;
    off_t saved_offset = lseek(stream->descriptor, 0, SEEK_CUR);
    if (saved_offset < 0 ||
        lseek(stream->descriptor, 0, SEEK_SET) < 0) {
        stream->flags |= AKAOS_FILE_ERROR;
        return EOF;
    }

    char *snapshot = realloc(stream->memstream_snapshot, size + 1U);
    if (!snapshot) {
        (void)lseek(stream->descriptor, saved_offset, SEEK_SET);
        stream->flags |= AKAOS_FILE_ERROR;
        return EOF;
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(stream->descriptor, snapshot + offset,
                             size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            int saved_errno = count < 0 ? errno : EIO;
            (void)lseek(stream->descriptor, saved_offset, SEEK_SET);
            stream->memstream_snapshot = snapshot;
            errno = saved_errno;
            stream->flags |= AKAOS_FILE_ERROR;
            return EOF;
        }
        offset += (size_t)count;
    }
    snapshot[size] = '\0';
    if (lseek(stream->descriptor, saved_offset, SEEK_SET) < 0) {
        stream->memstream_snapshot = snapshot;
        stream->flags |= AKAOS_FILE_ERROR;
        return EOF;
    }
    stream->memstream_snapshot = snapshot;
    *stream->memstream_buffer = snapshot;
    *stream->memstream_size = size;
    return 0;
}

int fseeko(FILE *stream, off_t offset, int whence) {
    if (!stream_is_valid(stream)) return -1;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        stream->flags |= AKAOS_FILE_ERROR;
        return -1;
    }
    if (lseek(stream->descriptor, offset, whence) < 0) {
        stream->flags |= AKAOS_FILE_ERROR;
        return -1;
    }
    stream->flags &= ~(AKAOS_FILE_EOF | AKAOS_FILE_PUSHBACK);
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    return fseeko(stream, (off_t)offset, whence);
}

off_t ftello(FILE *stream) {
    if (!stream_is_valid(stream)) return (off_t)-1;
    off_t position = lseek(stream->descriptor, 0, SEEK_CUR);
    if (position < 0) {
        stream->flags |= AKAOS_FILE_ERROR;
        return (off_t)-1;
    }
    if (stream->flags & AKAOS_FILE_PUSHBACK) --position;
    return position;
}

int fseeko64(FILE *stream, off_t offset, int whence) {
    return fseeko(stream, offset, whence);
}

off_t ftello64(FILE *stream) {
    return ftello(stream);
}

long ftell(FILE *stream) {
    off_t position = ftello(stream);
    if (position < 0) return -1L;
    long result = (long)position;
    if ((off_t)result != position) {
        errno = EOVERFLOW;
        if (stream) stream->flags |= AKAOS_FILE_ERROR;
        return -1L;
    }
    return result;
}

void rewind(FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return;
    }
    (void)fseeko(stream, 0, SEEK_SET);
    clearerr(stream);
}

int fgetpos(FILE *stream, fpos_t *position) {
    if (!position) {
        errno = EINVAL;
        return -1;
    }
    off_t current = ftello(stream);
    if (current < 0) return -1;
    *position = (fpos_t)current;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *position) {
    if (!position) {
        errno = EINVAL;
        return -1;
    }
    return fseeko(stream, (off_t)*position, SEEK_SET);
}

int setvbuf(FILE *stream, char *buffer, int mode, size_t size) {
    (void)buffer;
    (void)size;
    if (!stream_is_valid(stream)) return -1;
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
        errno = EINVAL;
        return -1;
    }
    /* Current streams use direct syscalls, so all accepted modes are safely
     * unbuffered until the libc stream-buffer implementation is added. */
    return 0;
}

void setbuf(FILE *stream, char *buffer) {
    (void)setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF,
                  buffer ? BUFSIZ : 0U);
}

int fclose(FILE *stream) {
    if (!stream_is_valid(stream)) return EOF;
    int result = 0;
    if (fflush(stream) == EOF) result = EOF;
    if (close(stream->descriptor) < 0) result = EOF;
    int owned = (stream->flags & AKAOS_FILE_OWNED) != 0U;
    stream->descriptor = -1;
    if (owned) free(stream);
    return result;
}

int feof(FILE *stream) {
    return stream ? (int)((stream->flags & AKAOS_FILE_EOF) != 0U) : 0;
}

int ferror(FILE *stream) {
    return stream ? (int)((stream->flags & AKAOS_FILE_ERROR) != 0U) : 0;
}

void clearerr(FILE *stream) {
    if (stream) stream->flags &= ~(AKAOS_FILE_EOF | AKAOS_FILE_ERROR);
}

int fileno(FILE *stream) {
    if (!stream_is_valid(stream)) return -1;
    return stream->descriptor;
}

size_t fread_unlocked(void *buffer, size_t size, size_t count, FILE *stream) {
    if (size == 0U || count == 0U) return 0U;
    if (!buffer || !stream_is_valid(stream) || count > SIZE_MAX / size) {
        if (count > 0U && size > 0U && !buffer) errno = EFAULT;
        if (stream) stream->flags |= AKAOS_FILE_ERROR;
        return 0U;
    }
    size_t wanted = size * count;
    size_t completed = 0U;
    unsigned char *bytes = (unsigned char *)buffer;
    if (stream->pushback_offset < stream->pushback_length) {
        size_t pending = stream->pushback_length - stream->pushback_offset;
        if (pending > wanted) pending = wanted;
        memcpy(bytes, stream->pushback_bytes + stream->pushback_offset, pending);
        completed += pending;
        stream->pushback_offset += (unsigned int)pending;
        if (stream->pushback_offset >= stream->pushback_length) {
            stream->pushback_length = 0U;
            stream->pushback_offset = 0U;
            stream->flags &= ~AKAOS_FILE_PUSHBACK;
        }
    }
    while (completed < wanted) {
        size_t chunk = wanted - completed;
        if (chunk > AKAOS_SYSCALL_MAX_IO)
            chunk = AKAOS_SYSCALL_MAX_IO;
        ssize_t result = read(stream->descriptor, bytes + completed,
                              chunk);
        if (result < 0) {
            stream->flags |= AKAOS_FILE_ERROR;
            break;
        }
        if (result == 0) {
            stream->flags |= AKAOS_FILE_EOF;
            break;
        }
        completed += (size_t)result;
    }
    return completed / size;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream_lock_acquire(stream)) return 0U;
    size_t result = fread_unlocked(buffer, size, count, stream);
    stream_lock_release(stream);
    return result;
}

size_t fwrite_unlocked(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (size == 0U || count == 0U) return 0U;
    if (!buffer || !stream_is_valid(stream) || count > SIZE_MAX / size) {
        if (count > 0U && size > 0U && !buffer) errno = EFAULT;
        if (stream) stream->flags |= AKAOS_FILE_ERROR;
        return 0U;
    }
    size_t wanted = size * count;
    size_t completed = 0U;
    const unsigned char *bytes = (const unsigned char *)buffer;
    while (completed < wanted) {
        size_t chunk = wanted - completed;
        if (chunk > AKAOS_SYSCALL_MAX_IO)
            chunk = AKAOS_SYSCALL_MAX_IO;
        ssize_t result = write(stream->descriptor, bytes + completed,
                               chunk);
        if (result <= 0) {
            stream->flags |= AKAOS_FILE_ERROR;
            break;
        }
        completed += (size_t)result;
    }
    return completed / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream_lock_acquire(stream)) return 0U;
    size_t result = fwrite_unlocked(buffer, size, count, stream);
    stream_lock_release(stream);
    return result;
}

int fgetc_unlocked(FILE *stream) {
    if (!stream_is_valid(stream)) return EOF;
    if (stream->pushback_offset < stream->pushback_length) {
        int character = stream->pushback_bytes[stream->pushback_offset++];
        if (stream->pushback_offset >= stream->pushback_length) {
            stream->pushback_length = 0U;
            stream->pushback_offset = 0U;
            stream->flags &= ~AKAOS_FILE_PUSHBACK;
        }
        return character;
    }
    unsigned char character;
    ssize_t result = read(stream->descriptor, &character, 1U);
    if (result == 1) return character;
    if (result == 0) stream->flags |= AKAOS_FILE_EOF;
    else stream->flags |= AKAOS_FILE_ERROR;
    return EOF;
}

int getc_unlocked(FILE *stream) { return fgetc_unlocked(stream); }
int getchar_unlocked(void) { return fgetc_unlocked(stdin); }

int fgetc(FILE *stream) {
    if (!stream_lock_acquire(stream)) return EOF;
    int result = fgetc_unlocked(stream);
    stream_lock_release(stream);
    return result;
}

int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int character, FILE *stream) {
    if (!stream_lock_acquire(stream)) return EOF;
    if (character == EOF ||
        stream->pushback_offset < stream->pushback_length) {
        stream_lock_release(stream);
        return EOF;
    }
    stream->pushback_bytes[0] = (unsigned char)character;
    stream->pushback_length = 1U;
    stream->pushback_offset = 0U;
    stream->flags |= AKAOS_FILE_PUSHBACK;
    stream->flags &= ~AKAOS_FILE_EOF;
    stream_lock_release(stream);
    return (unsigned char)character;
}

wint_t fgetwc(FILE *stream) {
    if (!stream_is_valid(stream)) return WEOF;
    stream->wide_orientation = 1;
    char encoded[4];
    int first = fgetc(stream);
    if (first == EOF) return WEOF;
    encoded[0] = (char)(unsigned char)first;
    wchar_t result_character = 0;
    size_t converted = mbrtowc(&result_character, encoded, 1U, NULL);
    size_t length = 1U;
    while (converted == (size_t)-2 && length < sizeof(encoded)) {
        int next = fgetc(stream);
        if (next == EOF) {
            stream->flags |= AKAOS_FILE_ERROR;
            return WEOF;
        }
        encoded[length++] = (char)(unsigned char)next;
        converted = mbrtowc(&result_character, encoded, length, NULL);
    }
    if (converted == (size_t)-1 || converted == (size_t)-2) {
        stream->flags |= AKAOS_FILE_ERROR;
        return WEOF;
    }
    return (wint_t)result_character;
}

wint_t getwc(FILE *stream) { return fgetwc(stream); }
wint_t getwchar(void) { return fgetwc(stdin); }

wint_t ungetwc(wint_t character, FILE *stream) {
    if (character == WEOF || !stream_is_valid(stream) ||
        stream->pushback_offset < stream->pushback_length) return WEOF;
    char encoded[4];
    size_t length = wcrtomb(encoded, (wchar_t)character, NULL);
    if (length == (size_t)-1 || length == 0U || length > sizeof(encoded)) {
        stream->flags |= AKAOS_FILE_ERROR;
        return WEOF;
    }
    memcpy(stream->pushback_bytes, encoded, length);
    stream->pushback_length = (unsigned int)length;
    stream->pushback_offset = 0U;
    stream->flags |= AKAOS_FILE_PUSHBACK;
    stream->flags &= ~AKAOS_FILE_EOF;
    stream->wide_orientation = 1;
    return character;
}

int fputc_unlocked(int character, FILE *stream) {
    unsigned char byte = (unsigned char)character;
    if (fwrite_unlocked(&byte, 1U, 1U, stream) != 1U) return EOF;
    return byte;
}

int putc_unlocked(int character, FILE *stream) {
    return fputc_unlocked(character, stream);
}
int putchar_unlocked(int character) { return fputc_unlocked(character, stdout); }

int fputc(int character, FILE *stream) {
    if (!stream_lock_acquire(stream)) return EOF;
    int result = fputc_unlocked(character, stream);
    stream_lock_release(stream);
    return result;
}

int putc(int character, FILE *stream) { return fputc(character, stream); }
int putchar(int character) { return fputc(character, stdout); }

wint_t fputwc(wchar_t character, FILE *stream) {
    if (!stream_is_valid(stream)) return WEOF;
    stream->wide_orientation = 1;
    char encoded[4];
    size_t length = wcrtomb(encoded, character, NULL);
    if (length == (size_t)-1 || length == 0U ||
        fwrite(encoded, 1U, length, stream) != length) return WEOF;
    return (wint_t)character;
}

wint_t putwc(wchar_t character, FILE *stream) {
    return fputwc(character, stream);
}

wint_t putwchar(wchar_t character) { return fputwc(character, stdout); }

char *fgets(char *buffer, int capacity, FILE *stream) {
    if (!buffer || capacity <= 0 || !stream_is_valid(stream)) {
        errno = EINVAL;
        return NULL;
    }
    if (!stream_lock_acquire(stream)) return NULL;
    int count = 0;
    while (count + 1 < capacity) {
        int character = fgetc(stream);
        if (character == EOF) break;
        buffer[count++] = (char)character;
        if (character == '\n') break;
    }
    if (count == 0) {
        stream_lock_release(stream);
        return NULL;
    }
    buffer[count] = '\0';
    stream_lock_release(stream);
    return buffer;
}

ssize_t getdelim(char **lineptr, size_t *capacity, int delimiter,
                 FILE *stream) {
    if (!lineptr || !capacity || !stream) {
        errno = EINVAL;
        return -1;
    }
    if (!stream_lock_acquire(stream)) return -1;

    if (!*lineptr || *capacity == 0U) {
        char *buffer = (char *)realloc(*lineptr, 128U);
        if (!buffer) {
            stream_lock_release(stream);
            return -1;
        }
        *lineptr = buffer;
        *capacity = 128U;
    }
    (*lineptr)[0] = '\0';

    size_t length = 0U;
    for (;;) {
        if (length >= (size_t)INTPTR_MAX || length == SIZE_MAX - 1U) {
            errno = EOVERFLOW;
            stream_lock_release(stream);
            return -1;
        }
        if (length + 1U >= *capacity) {
            size_t new_capacity = *capacity;
            if (new_capacity < 128U) new_capacity = 128U;
            while (new_capacity <= length + 1U) {
                if (new_capacity > SIZE_MAX / 2U) {
                    new_capacity = length + 2U;
                    break;
                }
                new_capacity *= 2U;
            }
            char *buffer = (char *)realloc(*lineptr, new_capacity);
            if (!buffer) {
                stream_lock_release(stream);
                return -1;
            }
            *lineptr = buffer;
            *capacity = new_capacity;
        }

        int character = fgetc_unlocked(stream);
        if (character == EOF) {
            if (length == 0U) {
                stream_lock_release(stream);
                return -1;
            }
            break;
        }
        (*lineptr)[length++] = (char)character;
        (*lineptr)[length] = '\0';
        if ((unsigned char)character == (unsigned char)delimiter) break;
    }

    (*lineptr)[length] = '\0';
    stream_lock_release(stream);
    return (ssize_t)length;
}

ssize_t getline(char **lineptr, size_t *capacity, FILE *stream) {
    return getdelim(lineptr, capacity, '\n', stream);
}

int fputs(const char *text, FILE *stream) {
    if (!text) {
        errno = EINVAL;
        return EOF;
    }
    size_t length = strlen(text);
    return fwrite(text, 1U, length, stream) == length ? 0 : EOF;
}

wchar_t *fgetws(wchar_t *buffer, int capacity, FILE *stream) {
    if (!buffer || capacity <= 0 || !stream_is_valid(stream)) {
        errno = EINVAL;
        return NULL;
    }
    stream->wide_orientation = 1;
    int count = 0;
    while (count + 1 < capacity) {
        wint_t character = fgetwc(stream);
        if (character == WEOF) break;
        buffer[count++] = (wchar_t)character;
        if (character == (wint_t)'\n') break;
    }
    if (count == 0) return NULL;
    buffer[count] = L'\0';
    return buffer;
}

int fputws(const wchar_t *text, FILE *stream) {
    if (!text || !stream_is_valid(stream)) {
        errno = EINVAL;
        return EOF;
    }
    stream->wide_orientation = 1;
    for (size_t index = 0; text[index] != L'\0'; ++index) {
        if (fputwc(text[index], stream) == WEOF) return EOF;
    }
    return 0;
}

int fwide(FILE *stream, int mode) {
    if (!stream) return 0;
    if (stream->wide_orientation == 0 && mode != 0)
        stream->wide_orientation = mode > 0 ? 1 : -1;
    return stream->wide_orientation;
}

int puts(const char *text) {
    if (!text) text = "(null)";
    if (fputs(text, stdout) == EOF || fputc('\n', stdout) == EOF) return EOF;
    return 0;
}

int vfscanf(FILE *stream, const char *format, va_list ap) {
    if (!stream_is_valid(stream) || !format) return EOF;
    char input[2048];
    size_t length = 0U;
    while (length + 1U < sizeof(input)) {
        int character = fgetc(stream);
        if (character == EOF) break;
        input[length++] = (char)character;
        if (character == '\n') break;
    }
    if (length == 0U) return EOF;
    input[length] = '\0';
    return vsscanf(input, format, ap);
}

int fscanf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vfscanf(stream, format, ap);
    va_end(ap);
    return result;
}

int scanf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vfscanf(stdin, format, ap);
    va_end(ap);
    return result;
}

void perror(const char *s) {
    extern char *strerror(int errnum);
    if (s && *s) {
        fdprintf(STDERR_FILENO, "%s: %s\n", s, strerror(errno));
    } else {
        fdprintf(STDERR_FILENO, "%s\n", strerror(errno));
    }
}

struct popen_entry {
    FILE *stream;
    pid_t pid;
    struct popen_entry *next;
};
static struct popen_entry *s_popen_list = NULL;

FILE *popen(const char *command, const char *mode) {
    if (!command || !mode) {
        errno = EINVAL;
        return NULL;
    }
    int read_mode = (mode[0] == 'r');
    int write_mode = (mode[0] == 'w');
    if (!read_mode && !write_mode) {
        errno = EINVAL;
        return NULL;
    }
    int pfd[2];
    if (pipe(pfd) < 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }

    if (pid == 0) {
        if (read_mode) {
            close(pfd[0]);
            if (pfd[1] != STDOUT_FILENO) {
                dup2(pfd[1], STDOUT_FILENO);
                close(pfd[1]);
            }
        } else {
            close(pfd[1]);
            if (pfd[0] != STDIN_FILENO) {
                dup2(pfd[0], STDIN_FILENO);
                close(pfd[0]);
            }
        }
        char *argv[] = {"/bin/akash", "-c", (char *)command, NULL};
        execv(argv[0], argv);
        _exit(127);
    }

    FILE *fp = NULL;
    if (read_mode) {
        close(pfd[1]);
        fp = fdopen(pfd[0], "r");
    } else {
        close(pfd[0]);
        fp = fdopen(pfd[1], "w");
    }
    if (!fp) return NULL;

    struct popen_entry *entry = (struct popen_entry *)malloc(sizeof(struct popen_entry));
    if (entry) {
        entry->stream = fp;
        entry->pid = pid;
        entry->next = s_popen_list;
        s_popen_list = entry;
    }
    return fp;
}

int pclose(FILE *stream) {
    if (!stream) return -1;
    struct popen_entry **prev = &s_popen_list;
    struct popen_entry *curr = s_popen_list;
    pid_t pid = -1;
    while (curr) {
        if (curr->stream == stream) {
            pid = curr->pid;
            *prev = curr->next;
            free(curr);
            break;
        }
        prev = &curr->next;
        curr = curr->next;
    }
    (void)fclose(stream);
    if (pid < 0) return -1;

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}

int __isoc99_sscanf(const char *input, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf(input, format, ap);
    va_end(ap);
    return ret;
}

int __isoc99_vsscanf(const char *input, const char *format, va_list ap) {
    return vsscanf(input, format, ap);
}

int __isoc99_fscanf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf(stream, format, ap);
    va_end(ap);
    return ret;
}

int __isoc99_vfscanf(FILE *stream, const char *format, va_list ap) {
    return vfscanf(stream, format, ap);
}

int __isoc99_scanf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf(stdin, format, ap);
    va_end(ap);
    return ret;
}

int __isoc99_vscanf(const char *format, va_list ap) {
    return vfscanf(stdin, format, ap);
}
