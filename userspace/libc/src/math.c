#include <math.h>
#include <errno.h>
#include <fenv.h>
#include <stdint.h>

static double akaos_round_integral_double(double value, int ceiling) {
    union { double value; uint64_t bits; } input = {.value = value};
    uint64_t magnitude = input.bits & UINT64_C(0x7fffffffffffffff);
    uint64_t exponent_field = (magnitude >> 52U) & UINT64_C(0x7ff);
    if (exponent_field == UINT64_C(0x7ff) || magnitude == 0U) return value;
    int exponent = (int)exponent_field - 1023;
    if (exponent < 0) {
        if (ceiling)
            return (input.bits >> 63U) ? -0.0 : 1.0;
        return (input.bits >> 63U) ? -1.0 : 0.0;
    }
    if (exponent >= 52) return value;
    unsigned shift = (unsigned)(52 - exponent);
    uint64_t mask = (UINT64_C(1) << shift) - 1U;
    if ((input.bits & mask) == 0U) return value;
    uint64_t rounded = input.bits & ~mask;
    if (ceiling && (input.bits >> 63U) == 0U)
        rounded += UINT64_C(1) << shift;
    if (!ceiling && (input.bits >> 63U) != 0U)
        rounded += UINT64_C(1) << shift;
    input.bits = rounded;
    return input.value;
}

static float akaos_round_integral_float(float value, int ceiling) {
    union { float value; uint32_t bits; } input = {.value = value};
    uint32_t magnitude = input.bits & UINT32_C(0x7fffffff);
    uint32_t exponent_field = (magnitude >> 23U) & UINT32_C(0xff);
    if (exponent_field == UINT32_C(0xff) || magnitude == 0U) return value;
    int exponent = (int)exponent_field - 127;
    if (exponent < 0) {
        if (ceiling)
            return (input.bits >> 31U) ? -0.0f : 1.0f;
        return (input.bits >> 31U) ? -1.0f : 0.0f;
    }
    if (exponent >= 23) return value;
    unsigned shift = (unsigned)(23 - exponent);
    uint32_t mask = (UINT32_C(1) << shift) - 1U;
    if ((input.bits & mask) == 0U) return value;
    uint32_t rounded = input.bits & ~mask;
    if ((ceiling && (input.bits >> 31U) == 0U) ||
        (!ceiling && (input.bits >> 31U) != 0U))
        rounded += UINT32_C(1) << shift;
    input.bits = rounded;
    return input.value;
}

static double akaos_trunc_integral_double(double value) {
    union { double value; uint64_t bits; } input = {.value = value};
    uint64_t magnitude = input.bits & UINT64_C(0x7fffffffffffffff);
    uint64_t exponent_field = (magnitude >> 52U) & UINT64_C(0x7ff);
    if (exponent_field == UINT64_C(0x7ff) || magnitude == 0U) return value;
    int exponent = (int)exponent_field - 1023;
    if (exponent < 0) {
        input.bits &= UINT64_C(0x8000000000000000);
        return input.value;
    }
    if (exponent >= 52) return value;
    unsigned shift = (unsigned)(52 - exponent);
    input.bits &= ~((UINT64_C(1) << shift) - 1U);
    return input.value;
}

static float akaos_trunc_integral_float(float value) {
    union { float value; uint32_t bits; } input = {.value = value};
    uint32_t magnitude = input.bits & UINT32_C(0x7fffffff);
    uint32_t exponent_field = (magnitude >> 23U) & UINT32_C(0xff);
    if (exponent_field == UINT32_C(0xff) || magnitude == 0U) return value;
    int exponent = (int)exponent_field - 127;
    if (exponent < 0) {
        input.bits &= UINT32_C(0x80000000);
        return input.value;
    }
    if (exponent >= 23) return value;
    unsigned shift = (unsigned)(23 - exponent);
    input.bits &= ~((UINT32_C(1) << shift) - 1U);
    return input.value;
}

double floor(double value) { return akaos_round_integral_double(value, 0); }
double ceil(double value) { return akaos_round_integral_double(value, 1); }
float floorf(float value) { return akaos_round_integral_float(value, 0); }
float ceilf(float value) { return akaos_round_integral_float(value, 1); }
double trunc(double value) { return akaos_trunc_integral_double(value); }
float truncf(float value) { return akaos_trunc_integral_float(value); }

double modf(double value, double *integral_part) {
    if (__builtin_isnan(value)) {
        if (integral_part) *integral_part = value;
        return value;
    }
    if (__builtin_isinf(value)) {
        if (integral_part) *integral_part = value;
        return signbit(value) ? -0.0 : 0.0;
    }
    double integral = trunc(value);
    if (integral_part) *integral_part = integral;
    double fraction = value - integral;
    if (fraction == 0.0) return signbit(value) ? -0.0 : 0.0;
    return fraction;
}

float modff(float value, float *integral_part) {
    if (__builtin_isnan(value)) {
        if (integral_part) *integral_part = value;
        return value;
    }
    if (__builtin_isinf(value)) {
        if (integral_part) *integral_part = value;
        return signbit(value) ? -0.0f : 0.0f;
    }
    float integral = truncf(value);
    if (integral_part) *integral_part = integral;
    float fraction = value - integral;
    if (fraction == 0.0f) return signbit(value) ? -0.0f : 0.0f;
    return fraction;
}

long double truncl(long double value) {
    if (!__builtin_isfinite(value) || value == 0.0L) return value;
    if (value > (long double)INT64_MAX || value < (long double)INT64_MIN)
        return value;
    int64_t integral = (int64_t)value;
    if (integral == 0 && signbit(value)) return -0.0L;
    return (long double)integral;
}

long double modfl(long double value, long double *integral_part) {
    if (__builtin_isnan(value)) {
        if (integral_part) *integral_part = value;
        return value;
    }
    if (__builtin_isinf(value)) {
        if (integral_part) *integral_part = value;
        return signbit(value) ? -0.0L : 0.0L;
    }
    long double integral = truncl(value);
    if (integral_part) *integral_part = integral;
    long double fraction = value - integral;
    if (fraction == 0.0L) return signbit(value) ? -0.0L : 0.0L;
    return fraction;
}

static long double akaos_round_nearest_even(long double value) {
    long double integral = truncl(value);
    long double fraction = fabsl(value - integral);
    if (fraction > 0.5L ||
        (fraction == 0.5L && fmodl(fabsl(integral), 2.0L) != 0.0L))
        integral += signbit(value) ? -1.0L : 1.0L;
    return integral;
}

static long double akaos_round_current(long double value) {
    switch (fegetround()) {
        case FE_DOWNWARD: return floorl(value);
        case FE_UPWARD: return ceill(value);
        case FE_TOWARDZERO: return truncl(value);
        default: return akaos_round_nearest_even(value);
    }
}

static long akaos_lrint_convert(long double value) {
    if (__builtin_isnan(value)) {
        errno = EDOM;
        return 0L;
    }
    if (__builtin_isinf(value)) {
        errno = ERANGE;
        return signbit(value) ? LONG_MIN : LONG_MAX;
    }
    long double rounded = akaos_round_current(value);
    if (rounded < (long double)LONG_MIN) {
        errno = ERANGE;
        return LONG_MIN;
    }
    if (rounded > (long double)LONG_MAX) {
        errno = ERANGE;
        return LONG_MAX;
    }
    return (long)rounded;
}

long lrint(double value) { return akaos_lrint_convert((long double)value); }
long lrintf(float value) { return akaos_lrint_convert((long double)value); }
long lrintl(long double value) { return akaos_lrint_convert(value); }

double round(double value) {
    if (!__builtin_isfinite(value) || value == 0.0) return value;
    double magnitude = fabs(value);
    if (magnitude >= 4503599627370496.0) return value;
    double integral = floor(magnitude + 0.5);
    return signbit(value) ? -integral : integral;
}

float roundf(float value) {
    if (!__builtin_isfinite(value) || value == 0.0f) return value;
    float magnitude = fabsf(value);
    if (magnitude >= 8388608.0f) return value;
    float integral = floorf(magnitude + 0.5f);
    return signbit(value) ? -integral : integral;
}

long double roundl(long double value) {
    if (!__builtin_isfinite(value) || value == 0.0L) return value;
    long double magnitude = fabsl(value);
    /* x86_64 akaOS uses the 80-bit extended format, whose representable
     * values at and above 2^63 are already integral. */
    if (magnitude >= 9223372036854775808.0L) return value;
    int64_t integral = (int64_t)magnitude;
    if (magnitude - (long double)integral >= 0.5L) ++integral;
    long double result = (long double)integral;
    if (result == 0.0L && signbit(value)) return -0.0L;
    return signbit(value) ? -result : result;
}

static double akaos_log_series(double mantissa, int exponent) {
    const double ln2 = 0.693147180559945309417232121458176568;
    const double sqrt_half = 0.707106781186547524400844362104849039;
    if (mantissa < sqrt_half) {
        mantissa *= 2.0;
        --exponent;
    }
    double ratio = (mantissa - 1.0) / (mantissa + 1.0);
    double square = ratio * ratio;
    double term = ratio;
    double sum = term;
    for (unsigned divisor = 3U; divisor <= 81U; divisor += 2U) {
        term *= square;
        sum += term / (double)divisor;
    }
    return (2.0 * sum) + ((double)exponent * ln2);
}

double log(double value) {
    if (__builtin_isnan(value)) return value;
    if (value < 0.0) return __builtin_nan("");
    if (value == 0.0) return -HUGE_VAL;
    if (__builtin_isinf(value)) return value;
    int exponent = 0;
    double mantissa = frexp(value, &exponent);
    return akaos_log_series(mantissa * 2.0, exponent - 1);
}

float logf(float value) { return (float)log((double)value); }
long double logl(long double value) { return (long double)log((double)value); }

double log2(double value) {
    const double inverse_ln2 = 1.442695040888963407359924681001892137;
    return log(value) * inverse_ln2;
}

float log2f(float value) { return (float)log2((double)value); }
long double log2l(long double value) { return (long double)log2((double)value); }

double log10(double value) {
    const double inverse_ln10 = 0.434294481903251827651128918916605082;
    return log(value) * inverse_ln10;
}

float log10f(float value) { return (float)log10((double)value); }
long double log10l(long double value) {
    return (long double)log10((double)value);
}

long double floorl(long double value) {
    if (!__builtin_isfinite(value) || value == 0.0L) return value;
    if (value > (long double)INT64_MAX ||
        value < (long double)INT64_MIN)
        return value;
    int64_t integral = (int64_t)value;
    if ((long double)integral > value) --integral;
    return (long double)integral;
}

long double ceill(long double value) {
    if (!__builtin_isfinite(value) || value == 0.0L) return value;
    if (value > (long double)INT64_MAX ||
        value < (long double)INT64_MIN)
        return value;
    int64_t integral = (int64_t)value;
    if ((long double)integral < value) ++integral;
    return (long double)integral;
}

double frexp(double value, int *exponent) {
    union { double value; uint64_t bits; } input = {.value = value};
    uint64_t field = (input.bits >> 52) & UINT64_C(0x7ff);
    if (field == UINT64_C(0x7ff) || (input.bits << 1) == 0U) {
        if (exponent) *exponent = 0;
        return value;
    }
    if (field == 0U) {
        uint64_t fraction = input.bits & UINT64_C(0x000fffffffffffff);
        unsigned highest = 0U;
        for (uint64_t scan = fraction; scan >>= 1U;) ++highest;
        uint64_t normalized = fraction << (52U - highest);
        input.bits = (input.bits & UINT64_C(0x8000000000000000)) |
                     (UINT64_C(1022) << 52U) |
                     (normalized & UINT64_C(0x000fffffffffffff));
        if (exponent) *exponent = (int)highest - 1073;
        return input.value;
    }
    input.bits = (input.bits & UINT64_C(0x800fffffffffffff)) |
                 (UINT64_C(1022) << 52U);
    if (exponent) *exponent = (int)field - 1022;
    return input.value;
}

float frexpf(float value, int *exponent) {
    union { float value; uint32_t bits; } input = {.value = value};
    uint32_t field = (input.bits >> 23U) & UINT32_C(0xff);
    if (field == UINT32_C(0xff) || (input.bits << 1U) == 0U) {
        if (exponent) *exponent = 0;
        return value;
    }
    if (field == 0U) {
        uint32_t fraction = input.bits & UINT32_C(0x007fffff);
        unsigned highest = 0U;
        for (uint32_t scan = fraction; scan >>= 1U;) ++highest;
        uint32_t normalized = fraction << (23U - highest);
        input.bits = (input.bits & UINT32_C(0x80000000)) |
                     (UINT32_C(126) << 23U) |
                     (normalized & UINT32_C(0x007fffff));
        if (exponent) *exponent = (int)highest - 148;
        return input.value;
    }
    input.bits = (input.bits & UINT32_C(0x807fffff)) |
                 (UINT32_C(126) << 23U);
    if (exponent) *exponent = (int)field - 126;
    return input.value;
}

long double frexpl(long double value, int *exponent) {
    if (!__builtin_isfinite(value) || value == 0.0L) {
        if (exponent) *exponent = 0;
        return value;
    }
    int result_exponent = 0;
    while (value >= 1.0L || value <= -1.0L) {
        value *= 0.5L;
        ++result_exponent;
    }
    while (value > -0.5L && value < 0.5L) {
        value *= 2.0L;
        --result_exponent;
    }
    if (exponent) *exponent = result_exponent;
    return value;
}

float ldexpf(float value, int exponent) {
    if (value == 0.0f || !isfinite(value) || exponent == 0) return value;
    if (exponent > 300) {
        errno = ERANGE;
        return signbit(value) ? -HUGE_VALF : HUGE_VALF;
    }
    if (exponent < -300) {
        errno = ERANGE;
        return value * 0.0f;
    }
    while (exponent > 16) {
        value *= 0x1p16f;
        exponent -= 16;
        if (!isfinite(value)) {
            errno = ERANGE;
            return value;
        }
    }
    while (exponent < -16) {
        value *= 0x1p-16f;
        exponent += 16;
        if (value == 0.0f) {
            errno = ERANGE;
            return value;
        }
    }
    while (exponent > 0) {
        value *= 2.0f;
        --exponent;
    }
    while (exponent < 0) {
        value *= 0.5f;
        ++exponent;
    }
    if (value == 0.0f || fpclassify(value) == FP_SUBNORMAL) errno = ERANGE;
    return value;
}

double ldexp(double value, int exponent) {
    if (value == 0.0 || !isfinite(value) || exponent == 0) return value;
    if (exponent > 2100) {
        errno = ERANGE;
        return signbit(value) ? -HUGE_VAL : HUGE_VAL;
    }
    if (exponent < -2100) {
        errno = ERANGE;
        return value * 0.0;
    }
    while (exponent > 512) {
        value *= 0x1p512;
        exponent -= 512;
        if (!isfinite(value)) {
            errno = ERANGE;
            return value;
        }
    }
    while (exponent < -512) {
        value *= 0x1p-512;
        exponent += 512;
        if (value == 0.0) {
            errno = ERANGE;
            return value;
        }
    }
    while (exponent > 0) {
        value *= 2.0;
        --exponent;
    }
    while (exponent < 0) {
        value *= 0.5;
        ++exponent;
    }
    if (value == 0.0 || fpclassify(value) == FP_SUBNORMAL) errno = ERANGE;
    return value;
}

long double ldexpl(long double value, int exponent) {
    if (value == 0.0L || !isfinite(value) || exponent == 0) return value;
    if (exponent > 40000) {
        errno = ERANGE;
        return signbit(value) ? -HUGE_VALL : HUGE_VALL;
    }
    if (exponent < -40000) {
        errno = ERANGE;
        return value * 0.0L;
    }
    while (exponent > 512) {
        value *= 0x1p512L;
        exponent -= 512;
        if (!isfinite(value)) {
            errno = ERANGE;
            return value;
        }
    }
    while (exponent < -512) {
        value *= 0x1p-512L;
        exponent += 512;
        if (value == 0.0L) {
            errno = ERANGE;
            return value;
        }
    }
    while (exponent > 0) {
        value *= 2.0L;
        --exponent;
    }
    while (exponent < 0) {
        value *= 0.5L;
        ++exponent;
    }
    if (value == 0.0L || fpclassify(value) == FP_SUBNORMAL) errno = ERANGE;
    return value;
}

double fabs(double value) {
    union { double value; uint64_t bits; } input = {.value = value};
    input.bits &= UINT64_C(0x7fffffffffffffff);
    return input.value;
}

float fabsf(float value) {
    union { float value; uint32_t bits; } input = {.value = value};
    input.bits &= UINT32_C(0x7fffffff);
    return input.value;
}

long double fabsl(long double value) {
    return signbit(value) ? -value : value;
}

#define AKAOS_DEFINE_MINMAX(type, suffix) \
    type fmax##suffix(type left, type right) { \
        if (__builtin_isnan(left)) return right; \
        if (__builtin_isnan(right)) return left; \
        if (left > right) return left; \
        if (right > left) return right; \
        if (left == (type)0 && signbit(left) && !signbit(right)) return right; \
        return left; \
    } \
    type fmin##suffix(type left, type right) { \
        if (__builtin_isnan(left)) return right; \
        if (__builtin_isnan(right)) return left; \
        if (left < right) return left; \
        if (right < left) return right; \
        if (left == (type)0 && !signbit(left) && signbit(right)) return right; \
        return left; \
    }

AKAOS_DEFINE_MINMAX(double, )
AKAOS_DEFINE_MINMAX(float, f)
AKAOS_DEFINE_MINMAX(long double, l)
#undef AKAOS_DEFINE_MINMAX

static double akaos_sqrt_positive(double value) {
    if (value < DBL_MIN) {
        return akaos_sqrt_positive(value * 18014398509481984.0) *
               0.000000007450580596923828125;
    }

    /* Halving the IEEE-754 exponent gives a close initial square-root
     * estimate.  Newton iteration then recovers full double precision. */
    union { double value; uint64_t bits; } estimate = {.value = value};
    estimate.bits = (estimate.bits >> 1U) +
                    UINT64_C(0x1ff8000000000000);
    double result = estimate.value;
    for (unsigned iteration = 0; iteration < 7U; ++iteration)
        result = 0.5 * (result + value / result);
    return result;
}

double sqrt(double value) {
    if (__builtin_isnan(value) || value == 0.0 ||
        (__builtin_isinf(value) && !signbit(value)))
        return value;
    if (value < 0.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    return akaos_sqrt_positive(value);
}

float sqrtf(float value) {
    if (__builtin_isnan(value) || value == 0.0f ||
        (__builtin_isinf(value) && !signbit(value)))
        return value;
    if (value < 0.0f) {
        errno = EDOM;
        return __builtin_nanf("");
    }
    return (float)akaos_sqrt_positive((double)value);
}

long double sqrtl(long double value) {
    if (__builtin_isnan(value) || value == 0.0L ||
        (__builtin_isinf(value) && !signbit(value)))
        return value;
    if (value < 0.0L) {
        errno = EDOM;
        return __builtin_nanl("");
    }
    return (long double)akaos_sqrt_positive((double)value);
}

double cbrt(double value) {
    if (__builtin_isnan(value) || value == 0.0 || __builtin_isinf(value))
        return value;
    double magnitude = fabs(value);
    double result = exp(log(magnitude) / 3.0);
    for (unsigned iteration = 0; iteration < 5U; ++iteration)
        result = (2.0 * result + magnitude / (result * result)) / 3.0;
    return signbit(value) ? -result : result;
}

float cbrtf(float value) { return (float)cbrt((double)value); }
long double cbrtl(long double value) { return (long double)cbrt((double)value); }

static double akaos_atan_kernel(double value) {
    double square = value * value;
    double term = value;
    double sum = value;
    for (unsigned index = 1U; index <= 32U; ++index) {
        term *= -square;
        sum += term / (double)(2U * index + 1U);
    }
    return sum;
}

static double akaos_atan_positive(double value) {
    const double pi_over_4 = 0.785398163397448309615660845819875721;
    const double pi_over_2 = 1.570796326794896619231321691639751442;
    const double tan_pi_over_8 = 0.414213562373095048801688724209698079;
    if (value > 1.0) return pi_over_2 - akaos_atan_positive(1.0 / value);
    if (value > tan_pi_over_8)
        return pi_over_4 + akaos_atan_kernel((value - 1.0) / (value + 1.0));
    return akaos_atan_kernel(value);
}

double atan(double value) {
    if (__builtin_isnan(value)) return value;
    if (__builtin_isinf(value)) {
        const double pi_over_2 = 1.570796326794896619231321691639751442;
        return signbit(value) ? -pi_over_2 : pi_over_2;
    }
    double result = akaos_atan_positive(fabs(value));
    return signbit(value) ? -result : result;
}

float atanf(float value) { return (float)atan((double)value); }
long double atanl(long double value) { return (long double)atan((double)value); }

double atan2(double y, double x) {
    const double pi = 3.141592653589793238462643383279502884;
    const double pi_over_2 = 1.570796326794896619231321691639751442;
    const double pi_over_4 = 0.785398163397448309615660845819875721;

    if (__builtin_isnan(x) || __builtin_isnan(y)) return x + y;
    if (y == 0.0) {
        if (signbit(x)) return signbit(y) ? -pi : pi;
        return y;
    }
    if (x == 0.0) return signbit(y) ? -pi_over_2 : pi_over_2;

    int x_infinite = __builtin_isinf(x);
    int y_infinite = __builtin_isinf(y);
    double angle;
    if (x_infinite && y_infinite) {
        angle = signbit(x) ? 3.0 * pi_over_4 : pi_over_4;
    } else if (y_infinite) {
        angle = pi_over_2;
    } else if (x_infinite) {
        angle = signbit(x) ? pi : 0.0;
    } else {
        double ax = fabs(x);
        double ay = fabs(y);
        angle = ax >= ay ? atan(ay / ax) : pi_over_2 - atan(ax / ay);
        if (signbit(x)) angle = pi - angle;
    }
    return signbit(y) ? -angle : angle;
}

float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
long double atan2l(long double y, long double x) {
    return (long double)atan2((double)y, (double)x);
}

static double akaos_scale_exp(double value, int exponent) {
    /* exp's nearest-ln(2) reduction can produce n == 1024 just below the
     * finite overflow threshold; move one factor of two into the mantissa. */
    if (exponent > 1023) {
        value *= 2.0;
        --exponent;
    }
    if (exponent >= -1022) {
        union { double value; uint64_t bits; } scale;
        scale.bits = (uint64_t)(exponent + 1023) << 52U;
        return value * scale.value;
    }
    if (exponent < -1075) return 0.0;

    /* Round subnormal results in units of the least positive subnormal. */
    int unit_exponent = exponent + 1074;
    union { double value; uint64_t bits; } scale;
    scale.bits = (uint64_t)(unit_exponent + 1023) << 52U;
    double units = value * scale.value;
    double rounded = floor(units + 0.5);
    union { double value; uint64_t bits; } result;
    result.bits = (uint64_t)rounded;
    return result.value;
}

double exp(double value) {
    const double ln2_high = 0.69314718036912381649017333984375;
    const double ln2_low = 1.90821492927058770002e-10;
    const double inverse_ln2 = 1.442695040888963407359924681001892137;
    const double overflow_limit = 709.782712893383973096;
    const double underflow_limit = -745.13321910194110842;

    if (__builtin_isnan(value)) return value;
    if (__builtin_isinf(value)) return signbit(value) ? 0.0 : value;
    if (value > overflow_limit) {
        errno = ERANGE;
        return HUGE_VAL;
    }
    if (value < underflow_limit) {
        errno = ERANGE;
        return 0.0;
    }

    double scaled = value * inverse_ln2;
    int exponent = (int)(scaled + (scaled >= 0.0 ? 0.5 : -0.5));
    double reduced = (value - (double)exponent * ln2_high) -
                     (double)exponent * ln2_low;
    double term = 1.0;
    double sum = 1.0;
    for (unsigned index = 1U; index <= 20U; ++index) {
        term *= reduced / (double)index;
        sum += term;
    }
    return akaos_scale_exp(sum, exponent);
}

float expf(float value) { return (float)exp((double)value); }
long double expl(long double value) { return (long double)exp((double)value); }

double exp2(double value) {
    const double ln2 = 0.693147180559945309417232121458176568;
    return exp(value * ln2);
}

float exp2f(float value) { return (float)exp2((double)value); }
long double exp2l(long double value) { return (long double)exp2((double)value); }

static double akaos_sin_kernel(double value) {
    double square = value * value;
    double term = value;
    double sum = value;
    for (unsigned index = 1U; index <= 12U; ++index) {
        double first = (double)(2U * index);
        term *= -square / (first * (first + 1.0));
        sum += term;
    }
    return sum;
}

static double akaos_cos_kernel(double value) {
    double square = value * value;
    double term = 1.0;
    double sum = 1.0;
    for (unsigned index = 1U; index <= 12U; ++index) {
        double first = (double)(2U * index - 1U);
        term *= -square / (first * (first + 1.0));
        sum += term;
    }
    return sum;
}

static double akaos_sin_cos(double value, int cosine) {
    const double inverse_pi_over_2 = 0.636619772367581343075535053490057448;
    const double pi_over_2_high = 1.570796326734125614166;
    const double pi_over_2_low = 6.07710050650619224932e-11;
    if (!__builtin_isfinite(value)) {
        errno = EDOM;
        return __builtin_nan("");
    }

    /* This split-constant reducer covers the range used by graphics kernels
     * while retaining enough low bits for sub-pixel filter calculations. */
    if (fabs(value) > 1048576.0) {
        errno = ERANGE;
        return __builtin_nan("");
    }
    double quadrant_value = floor(value * inverse_pi_over_2 + 0.5);
    int quadrant = (int)quadrant_value % 4;
    if (quadrant < 0) quadrant += 4;
    double reduced = (value - quadrant_value * pi_over_2_high) -
                     quadrant_value * pi_over_2_low;

    if (cosine) quadrant = (quadrant + 1) % 4;
    switch (quadrant) {
    case 0: return akaos_sin_kernel(reduced);
    case 1: return akaos_cos_kernel(reduced);
    case 2: return -akaos_sin_kernel(reduced);
    default: return -akaos_cos_kernel(reduced);
    }
}

double sin(double value) { return akaos_sin_cos(value, 0); }
float sinf(float value) { return (float)sin((double)value); }
long double sinl(long double value) { return (long double)sin((double)value); }
double cos(double value) { return akaos_sin_cos(value, 1); }
float cosf(float value) { return (float)cos((double)value); }
long double cosl(long double value) { return (long double)cos((double)value); }

void sincos(double value, double *sine, double *cosp) {
    /* The argument reduction is shared, so both results come from one pass. */
    if (sine) *sine = akaos_sin_cos(value, 0);
    if (cosp) *cosp = akaos_sin_cos(value, 1);
}

double hypot(double x, double y) {
    double ax = fabs(x);
    double ay = fabs(y);
    if (__builtin_isinf(ax) || __builtin_isinf(ay)) return HUGE_VAL;
    if (__builtin_isnan(ax) || __builtin_isnan(ay)) return x + y;
    double larger = ax >= ay ? ax : ay;
    double smaller = ax >= ay ? ay : ax;
    if (larger == 0.0) return 0.0;
    double ratio = smaller / larger;
    return larger * sqrt(1.0 + ratio * ratio);
}

float hypotf(float x, float y) { return (float)hypot((double)x, (double)y); }
long double hypotl(long double x, long double y) {
    return (long double)hypot((double)x, (double)y);
}

double rint(double value) {
    return (double)akaos_round_current((long double)value);
}

float rintf(float value) {
    return (float)akaos_round_current((long double)value);
}

long double rintl(long double value) {
    return akaos_round_current(value);
}

double nearbyint(double value) {
    return (double)akaos_round_current((long double)value);
}

float nearbyintf(float value) {
    return (float)akaos_round_current((long double)value);
}

long double nearbyintl(long double value) {
    return akaos_round_current(value);
}

double fmod(double x, double y) {
    if (__builtin_isnan(x) || __builtin_isnan(y)) return x + y;
    if (y == 0.0 || __builtin_isinf(x)) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (__builtin_isinf(y) || x == 0.0) return x;

    double ax = fabs(x);
    double ay = fabs(y);
    if (ax < ay) return x;
    if (ax == ay) return signbit(x) ? -0.0 : 0.0;

    int x_exponent = 0;
    int y_exponent = 0;
    double x_mantissa = frexp(ax, &x_exponent);
    double y_mantissa = frexp(ay, &y_exponent);
    while (x_exponent > y_exponent) {
        if (x_mantissa >= y_mantissa) x_mantissa -= y_mantissa;
        if (x_mantissa == 0.0) return signbit(x) ? -0.0 : 0.0;
        x_mantissa *= 2.0;
        --x_exponent;
    }
    if (x_mantissa >= y_mantissa) x_mantissa -= y_mantissa;
    if (x_mantissa == 0.0) return signbit(x) ? -0.0 : 0.0;
    double result = akaos_scale_exp(x_mantissa, y_exponent);
    return signbit(x) ? -result : result;
}

float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }
long double fmodl(long double x, long double y) {
    return (long double)fmod((double)x, (double)y);
}

double asin(double value) {
    const double half_pi = 1.570796326794896619231321691639751442;
    if (__builtin_isnan(value)) return value;
    if (value > 1.0 || value < -1.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (value == 1.0) return half_pi;
    if (value == -1.0) return -half_pi;
    return atan2(value, sqrt((1.0 - value) * (1.0 + value)));
}

float asinf(float value) { return (float)asin((double)value); }
long double asinl(long double value) { return (long double)asin((double)value); }

double acos(double value) {
    if (__builtin_isnan(value)) return value;
    if (value > 1.0 || value < -1.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    return atan2(sqrt((1.0 - value) * (1.0 + value)), value);
}

float acosf(float value) { return (float)acos((double)value); }
long double acosl(long double value) { return (long double)acos((double)value); }

double tan(double value) {
    double numerator = sin(value);
    double denominator = cos(value);
    if (denominator == 0.0) {
        errno = ERANGE;
        return signbit(numerator) ? -HUGE_VAL : HUGE_VAL;
    }
    return numerator / denominator;
}

float tanf(float value) { return (float)tan((double)value); }
long double tanl(long double value) { return (long double)tan((double)value); }

double pow(double x, double y) {
    if (y == 0.0 || x == 1.0) return 1.0;
    if (__builtin_isnan(x) || __builtin_isnan(y)) return x + y;
    if (x == -1.0 && __builtin_isinf(y)) return 1.0;

    double ax = fabs(x);
    if (__builtin_isinf(y)) {
        if (ax == 1.0) return 1.0;
        if ((ax > 1.0) == (y > 0.0)) return HUGE_VAL;
        return 0.0;
    }

    int odd_integer = 0;
    if (x < 0.0) {
        if (floor(y) != y) {
            errno = EDOM;
            return __builtin_nan("");
        }
        odd_integer = fmod(fabs(y), 2.0) == 1.0;
    }
    if (x == 0.0) {
        if (y < 0.0) {
            errno = ERANGE;
            return signbit(x) && odd_integer ? -HUGE_VAL : HUGE_VAL;
        }
        return signbit(x) && odd_integer ? -0.0 : 0.0;
    }
    if (__builtin_isinf(x)) {
        if (y < 0.0) return signbit(x) && odd_integer ? -0.0 : 0.0;
        return signbit(x) && odd_integer ? -HUGE_VAL : HUGE_VAL;
    }

    double result = exp(y * log(ax));
    return x < 0.0 && odd_integer ? -result : result;
}

float powf(float x, float y) { return (float)pow((double)x, (double)y); }
long double powl(long double x, long double y) {
    return (long double)pow((double)x, (double)y);
}

double copysign(double magnitude, double sign) {
    return __builtin_copysign(magnitude, sign);
}

float copysignf(float magnitude, float sign) {
    return __builtin_copysignf(magnitude, sign);
}

long double copysignl(long double magnitude, long double sign) {
    return __builtin_copysignl(magnitude, sign);
}

static double akaos_expm1_series(double value) {
    double term = value;
    double sum = value;
    for (unsigned index = 2U; index <= 20U; ++index) {
        term *= value / (double)index;
        sum += term;
    }
    return sum;
}

double expm1(double value) {
    if (__builtin_isnan(value) || value == 0.0) return value;
    if (fabs(value) <= 0.5) return akaos_expm1_series(value);
    return exp(value) - 1.0;
}

float expm1f(float value) { return (float)expm1((double)value); }
long double expm1l(long double value) { return (long double)expm1((double)value); }

double log1p(double value) {
    if (__builtin_isnan(value) || value == 0.0) return value;
    if (value == -1.0) {
        errno = ERANGE;
        return -HUGE_VAL;
    }
    if (value < -1.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (__builtin_isinf(value)) return value;

    if (fabs(value) < 0.125) {
        double reduced = value / (2.0 + value);
        double square = reduced * reduced;
        double term = reduced;
        double sum = reduced;
        for (unsigned denominator = 3U; denominator <= 39U;
             denominator += 2U) {
            term *= square;
            sum += term / (double)denominator;
        }
        return 2.0 * sum;
    }
    return log(1.0 + value);
}

float log1pf(float value) { return (float)log1p((double)value); }
long double log1pl(long double value) { return (long double)log1p((double)value); }

double acosh(double value) {
    if (__builtin_isnan(value) || __builtin_isinf(value)) return value;
    if (value < 1.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (value == 1.0) return 0.0;
    if (value > 1.0e154) return log(value) + 0.69314718055994530942;
    double offset = value - 1.0;
    return log1p(offset + sqrt(offset) * sqrt(value + 1.0));
}

float acoshf(float value) { return (float)acosh((double)value); }
long double acoshl(long double value) { return (long double)acosh((double)value); }

double asinh(double value) {
    if (__builtin_isnan(value) || __builtin_isinf(value) || value == 0.0)
        return value;
    double magnitude = fabs(value);
    double result;
    if (magnitude > 1.0e154) {
        result = log(magnitude) + 0.69314718055994530942;
    } else {
        double hypotenuse = hypot(magnitude, 1.0);
        result = log1p(magnitude +
                       magnitude * magnitude / (1.0 + hypotenuse));
    }
    return copysign(result, value);
}

float asinhf(float value) { return (float)asinh((double)value); }
long double asinhl(long double value) { return (long double)asinh((double)value); }

double atanh(double value) {
    if (__builtin_isnan(value) || value == 0.0) return value;
    double magnitude = fabs(value);
    if (magnitude > 1.0) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (magnitude == 1.0) {
        errno = ERANGE;
        return copysign(HUGE_VAL, value);
    }
    return 0.5 * (log1p(value) - log1p(-value));
}

float atanhf(float value) { return (float)atanh((double)value); }
long double atanhl(long double value) { return (long double)atanh((double)value); }

double sinh(double value) {
    if (__builtin_isnan(value) || __builtin_isinf(value) || value == 0.0)
        return value;
    double magnitude = fabs(value);
    double result;
    if (magnitude < 0.5) {
        double square = magnitude * magnitude;
        double term = magnitude;
        result = magnitude;
        for (unsigned index = 1U; index <= 10U; ++index) {
            term *= square / ((double)(2U * index) *
                              (double)(2U * index + 1U));
            result += term;
        }
    } else {
        double positive_exp = exp(magnitude);
        if (__builtin_isinf(positive_exp)) return copysign(HUGE_VAL, value);
        result = 0.5 * (positive_exp - 1.0 / positive_exp);
    }
    return copysign(result, value);
}

float sinhf(float value) { return (float)sinh((double)value); }
long double sinhl(long double value) { return (long double)sinh((double)value); }

double cosh(double value) {
    if (__builtin_isnan(value)) return value;
    if (__builtin_isinf(value)) return HUGE_VAL;
    double magnitude = fabs(value);
    if (magnitude < 0.5) {
        double square = magnitude * magnitude;
        double term = 1.0;
        double sum = 1.0;
        for (unsigned index = 1U; index <= 10U; ++index) {
            term *= square / ((double)(2U * index - 1U) *
                              (double)(2U * index));
            sum += term;
        }
        return sum;
    }
    double positive_exp = exp(magnitude);
    if (__builtin_isinf(positive_exp)) return positive_exp;
    return 0.5 * (positive_exp + 1.0 / positive_exp);
}

float coshf(float value) { return (float)cosh((double)value); }
long double coshl(long double value) { return (long double)cosh((double)value); }

double tanh(double value) {
    if (__builtin_isnan(value) || value == 0.0) return value;
    double magnitude = fabs(value);
    if (__builtin_isinf(magnitude)) return copysign(1.0, value);
    double exp_minus_two = expm1(-2.0 * magnitude);
    double result = -exp_minus_two / (2.0 + exp_minus_two);
    return copysign(result, value);
}

float tanhf(float value) { return (float)tanh((double)value); }
long double tanhl(long double value) { return (long double)tanh((double)value); }

static int akaos_quotient_is_odd(double quotient) {
    if (!__builtin_isfinite(quotient)) return 0;
    return fmod(fabs(quotient), 2.0) == 1.0;
}

double remainder(double value, double divisor) {
    if (__builtin_isnan(value) || __builtin_isnan(divisor))
        return value + divisor;
    if (divisor == 0.0 || __builtin_isinf(value)) {
        errno = EDOM;
        return __builtin_nan("");
    }
    if (__builtin_isinf(divisor) || value == 0.0) return value;

    double result = fmod(value, divisor);
    if (result == 0.0 || __builtin_isnan(result)) return result;
    double magnitude = fabs(result);
    double distance_to_divisor = fabs(divisor) - magnitude;
    int above_half = magnitude > distance_to_divisor;
    int exactly_half = magnitude == distance_to_divisor;
    double truncated_quotient = (value - result) / divisor;
    if (above_half ||
        (exactly_half && akaos_quotient_is_odd(truncated_quotient)))
        result -= copysign(fabs(divisor), result);
    return result;
}

float remainderf(float value, float divisor) {
    return (float)remainder((double)value, (double)divisor);
}

long double remainderl(long double value, long double divisor) {
    return (long double)remainder((double)value, (double)divisor);
}

double remquo(double value, double divisor, int *quotient) {
    double result = remainder(value, divisor);
    if (quotient) {
        double truncated_quotient = (value - result) / divisor;
        if (!__builtin_isfinite(truncated_quotient)) {
            *quotient = 0;
        } else {
            int low_bits = (int)fmod(fabs(truncated_quotient), 8.0);
            *quotient = signbit(truncated_quotient) ? -low_bits : low_bits;
        }
    }
    return result;
}

float remquof(float value, float divisor, int *quotient) {
    return (float)remquo((double)value, (double)divisor, quotient);
}

long double remquol(long double value, long double divisor, int *quotient) {
    return (long double)remquo((double)value, (double)divisor, quotient);
}
