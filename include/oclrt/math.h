/*
 * OCL runtime math.h — implemented in src/libc/math.c.
 */
#ifndef OCL_FS_MATH_H
#define OCL_FS_MATH_H

#define OCL_INF __builtin_inf()
#define OCL_NAN __builtin_nan("")

#define INFINITY OCL_INF
#define NAN      OCL_NAN
#define HUGE_VAL OCL_INF

double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);
double sqrt(double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double asin(double x);
double acos(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double ldexp(double x, int e);
double frexp(double x, int *e);

#endif /* OCL_FS_MATH_H */
