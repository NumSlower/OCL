#ifndef OCL_NUMOS_MATH_H
#define OCL_NUMOS_MATH_H

static inline double fabs(double x) { return x < 0.0 ? -x : x; }
static inline double floor(double x) {
    long long i = (long long)x;
    if ((double)i > x) i--;
    return (double)i;
}
static inline double ceil(double x) {
    long long i = (long long)x;
    if ((double)i < x) i++;
    return (double)i;
}
static inline double fmod(double x, double y) {
    long long q;
    if (y == 0.0) return 0.0;
    q = (long long)(x / y);
    return x - ((double)q * y);
}
static inline double pow(double base, double exp) {
    long long n = (long long)exp;
    double out = 1.0;
    if ((double)n != exp) return 0.0;
    if (n < 0) return 0.0;
    while (n-- > 0) out *= base;
    return out;
}

#endif
