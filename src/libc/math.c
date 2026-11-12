/*
 * OCL freestanding math library — written from scratch.
 *
 * Accuracy target is ~1e-15 relative for the elementary functions, which
 * is what the OCL stdlib and its tests need. No host libm is used.
 */

#include <oclrt/stdint.h>
#include <oclrt/math.h>

typedef union { double d; uint64_t u; } DBits;

#define OCL_PI      3.14159265358979323846
#define OCL_LN2     0.69314718055994530942
#define OCL_SQRT2   1.41421356237309504880
#define OCL_INV_SQRT2 0.70710678118654752440

static int is_nan(double x) { return x != x; }
static int is_inf(double x) { return !is_nan(x) && (x > 1.7976931348623157e308 || x < -1.7976931348623157e308); }

double fabs(double x) {
    DBits b; b.d = x; b.u &= 0x7fffffffffffffffULL; return b.d;
}

double trunc(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    if (x >= 9223372036854775808.0 || x <= -9223372036854775808.0) return x;
    return (double)(int64_t)x;
}

double floor(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    double t = trunc(x);
    if (t > x) t -= 1.0;
    return t;
}

double ceil(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    double t = trunc(x);
    if (t < x) t += 1.0;
    return t;
}

double round(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double fmod(double x, double y) {
    if (is_nan(x) || is_nan(y) || y == 0.0 || is_inf(x)) return NAN;
    if (is_inf(y)) return x;

    double ax = fabs(x), ay = fabs(y);
    if (ax < ay) return x;

    double r = x - trunc(x / y) * y;
    while (fabs(r) >= ay) r -= (r > 0.0) ? ay : -ay;
    while (r != 0.0 && fabs(r) < ay && ((r > 0.0) != (x > 0.0))) r += (x > 0.0) ? ay : -ay;
    return r;
}

double ldexp(double x, int e) {
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x *= 0.5; e++; }
    return x;
}

double frexp(double x, int *e) {
    *e = 0;
    if (x == 0.0 || is_nan(x) || is_inf(x)) return x;

    DBits b; b.d = x;
    int ex = (int)((b.u >> 52) & 0x7ff);
    if (ex == 0) {
        /* subnormal: normalise first */
        double scaled = x * 18014398509481984.0;   /* 2^54 */
        double m = frexp(scaled, e);
        *e -= 54;
        return m;
    }
    *e = ex - 1022;
    b.u = (b.u & 0x800fffffffffffffULL) | (1022ULL << 52);
    return b.d;
}

double sqrt(double x) {
    if (is_nan(x)) return x;
    if (x < 0.0) return NAN;
    if (x == 0.0 || is_inf(x)) return x;

    DBits b; b.d = x;
    b.u = (b.u >> 1) + (0x3ff0000000000000ULL >> 1);
    double g = b.d;
    for (int i = 0; i < 6; i++) g = 0.5 * (g + x / g);
    return g;
}

double exp(double x) {
    if (is_nan(x)) return x;
    if (x > 709.782712893384) return INFINITY;
    if (x < -745.133219101941) return 0.0;

    int k = (int)(x / OCL_LN2 + ((x >= 0.0) ? 0.5 : -0.5));
    double r = x - (double)k * OCL_LN2;

    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 22; i++) { term *= r / (double)i; sum += term; }
    return ldexp(sum, k);
}

double log(double x) {
    if (is_nan(x)) return x;
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    if (is_inf(x)) return x;

    int e = 0;
    double m = frexp(x, &e);
    if (m < OCL_INV_SQRT2) { m *= 2.0; e--; }

    double z  = (m - 1.0) / (m + 1.0);
    double z2 = z * z;
    double term = z, sum = 0.0;
    for (int i = 1; i < 60; i += 2) { sum += term / (double)i; term *= z2; }
    return 2.0 * sum + (double)e * OCL_LN2;
}

double log10(double x) { return log(x) / 2.30258509299404568402; }

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (is_nan(x) || is_nan(y)) return NAN;
    if (x == 0.0) return (y > 0.0) ? 0.0 : INFINITY;
    if (x == 1.0) return 1.0;

    /* exact repeated squaring for integral exponents */
    if (y == (double)(int64_t)y && fabs(y) < 4096.0) {
        int64_t n = (int64_t)y;
        int neg = (n < 0);
        if (neg) n = -n;
        double r = 1.0, base = x;
        while (n) {
            if (n & 1) r *= base;
            base *= base;
            n >>= 1;
        }
        return neg ? 1.0 / r : r;
    }

    if (x < 0.0) return NAN;
    return exp(y * log(x));
}

static double reduce_two_pi(double x) {
    double k = round(x / (2.0 * OCL_PI));
    return x - k * (2.0 * OCL_PI);
}

double sin(double x) {
    if (is_nan(x) || is_inf(x)) return NAN;
    x = reduce_two_pi(x);

    double x2 = x * x, term = x, sum = x;
    for (int i = 1; i < 14; i++) {
        term *= -x2 / (double)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

double cos(double x) {
    if (is_nan(x) || is_inf(x)) return NAN;
    x = reduce_two_pi(x);

    double x2 = x * x, term = 1.0, sum = 1.0;
    for (int i = 1; i < 14; i++) {
        term *= -x2 / (double)((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

double tan(double x) { return sin(x) / cos(x); }

double atan(double x) {
    if (is_nan(x)) return x;
    if (is_inf(x)) return (x > 0.0) ? OCL_PI / 2.0 : -OCL_PI / 2.0;

    int neg = (x < 0.0);
    if (neg) x = -x;

    int invert = 0;
    if (x > 1.0) { x = 1.0 / x; invert = 1; }

    /* two half-angle reductions bring |x| below ~0.05 */
    x = x / (1.0 + sqrt(1.0 + x * x));
    x = x / (1.0 + sqrt(1.0 + x * x));

    double x2 = x * x, term = x, sum = x;
    for (int i = 1; i < 20; i++) {
        term *= -x2;
        sum += term / (double)(2 * i + 1);
    }
    sum *= 4.0;

    if (invert) sum = OCL_PI / 2.0 - sum;
    return neg ? -sum : sum;
}

double atan2(double y, double x) {
    if (x > 0.0) return atan(y / x);
    if (x < 0.0) return atan(y / x) + ((y >= 0.0) ? OCL_PI : -OCL_PI);
    if (y > 0.0) return OCL_PI / 2.0;
    if (y < 0.0) return -OCL_PI / 2.0;
    return 0.0;
}

double asin(double x) {
    if (is_nan(x)) return x;
    if (x > 1.0 || x < -1.0) return NAN;
    if (x == 1.0)  return OCL_PI / 2.0;
    if (x == -1.0) return -OCL_PI / 2.0;
    return atan(x / sqrt(1.0 - x * x));
}

double acos(double x) {
    if (is_nan(x)) return x;
    if (x > 1.0 || x < -1.0) return NAN;
    return OCL_PI / 2.0 - asin(x);
}

double sinh(double x) { double e = exp(x); return (e - 1.0 / e) * 0.5; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) * 0.5; }
double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
