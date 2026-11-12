/*
 * OCL freestanding numeric parsing: strtol, strtod, atoi.
 */
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

static unsigned long long parse_uint(const char *s, char **end, int base, int *neg_out) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;

    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    if (base < 2 || base > 36) { if (end) *end = (char *)s; errno = EINVAL; *neg_out = 0; return 0; }

    unsigned long long acc = 0;
    int any = 0;
    for (;; p++) {
        int d = digit_value((unsigned char)*p);
        if (d >= base) break;
        acc = acc * (unsigned long long)base + (unsigned long long)d;
        any = 1;
    }
    if (end) *end = (char *)(any ? p : s);
    *neg_out = neg;
    return acc;
}

long strtol(const char *s, char **end, int base) {
    int neg = 0;
    unsigned long long acc = parse_uint(s, end, base, &neg);
    return neg ? -(long)acc : (long)acc;
}

long long strtoll(const char *s, char **end, int base) {
    int neg = 0;
    unsigned long long acc = parse_uint(s, end, base, &neg);
    return neg ? -(long long)acc : (long long)acc;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    int neg = 0;
    return parse_uint(s, end, base, &neg);
}

double strtod(const char *s, char **end) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;

    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    double value = 0.0;
    int any = 0;

    while (*p >= '0' && *p <= '9') { value = value * 10.0 + (double)(*p - '0'); p++; any = 1; }

    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') {
            value += (double)(*p - '0') * scale;
            scale *= 0.1;
            p++;
            any = 1;
        }
    }

    if (any && (*p == 'e' || *p == 'E')) {
        const char *save = p;
        p++;
        int eneg = 0;
        if (*p == '+' || *p == '-') { eneg = (*p == '-'); p++; }
        int exp = 0, edigits = 0;
        while (*p >= '0' && *p <= '9') { exp = exp * 10 + (*p - '0'); p++; edigits = 1; }
        if (!edigits) {
            p = save;
        } else {
            double f = pow(10.0, (double)exp);
            value = eneg ? value / f : value * f;
        }
    }

    if (end) *end = (char *)(any ? p : s);
    return neg ? -value : value;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
