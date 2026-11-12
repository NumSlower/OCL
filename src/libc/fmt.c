/*
 * OCL freestanding formatted output — printf family from scratch.
 *
 * Supports: %d %i %u %o %x %X %c %s %p %f %F %e %E %g %G %%
 * Flags: - + space 0 #   Width: digits or *   Precision: .digits or .*
 * Length: hh h l ll z j t
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef void (*PutFn)(void *ctx, const char *s, size_t n);

/* ── sinks ───────────────────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} BufSink;

static void buf_put(void *ctx, const char *s, size_t n) {
    BufSink *b = (BufSink *)ctx;
    if (b->cap > 0) {
        size_t room = (b->len < b->cap - 1) ? (b->cap - 1 - b->len) : 0;
        size_t k    = (n < room) ? n : room;
        if (k) memcpy(b->buf + b->len, s, k);
    }
    b->len += n;
}

static void file_put(void *ctx, const char *s, size_t n) {
    if (n) fwrite(s, 1, n, (FILE *)ctx);
}

/* ── integer digits ──────────────────────────────────────────────── */

static size_t utoa(uint64_t v, unsigned base, int upper, char *out) {
    static const char lower[] = "0123456789abcdef";
    static const char upperd[] = "0123456789ABCDEF";
    const char *digits = upper ? upperd : lower;
    char tmp[72];
    size_t n = 0;

    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = digits[v % base]; v /= base; }

    for (size_t i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

/* ── floating point ──────────────────────────────────────────────── */

static double pow10i(int n) {
    double r = 1.0;
    for (int i = 0; i < n; i++) r *= 10.0;
    return r;
}

static int dec_exp(double v) {
    int e = 0;
    if (v == 0.0) return 0;
    while (v >= 10.0) { v /= 10.0; e++; }
    while (v < 1.0)   { v *= 10.0; e--; }
    return e;
}

/* Fixed notation, magnitude only. */
static size_t fmt_fixed(char *out, double v, int prec) {
    if (prec < 0)  prec = 6;
    if (prec > 17) prec = 17;

    uint64_t ip   = (uint64_t)v;
    double   frac = v - (double)ip;
    double   scale = pow10i(prec);
    double   scaled = frac * scale;
    uint64_t fp = (uint64_t)(scaled + 0.5);

    if ((double)fp >= scale) { fp -= (uint64_t)scale; ip += 1; }

    size_t n = utoa(ip, 10, 0, out);
    if (prec > 0) {
        char tmp[24];
        out[n++] = '.';
        for (int i = prec - 1; i >= 0; i--) { tmp[i] = (char)('0' + (int)(fp % 10)); fp /= 10; }
        memcpy(out + n, tmp, (size_t)prec);
        n += (size_t)prec;
    }
    return n;
}

/* Scientific notation, magnitude only. */
static size_t fmt_sci(char *out, double v, int prec, int upper) {
    if (prec < 0)  prec = 6;
    if (prec > 17) prec = 17;

    int exp = 0;
    if (v != 0.0) {
        while (v >= 10.0) { v /= 10.0; exp++; }
        while (v < 1.0)   { v *= 10.0; exp--; }
    }

    double   scale  = pow10i(prec);
    double   scaled = v * scale;
    uint64_t d      = (uint64_t)(scaled + 0.5);
    uint64_t limit  = (uint64_t)scale * 10u;

    if (d >= limit) { d /= 10u; exp++; }

    char digits[24];
    for (int i = prec; i >= 0; i--) { digits[i] = (char)('0' + (int)(d % 10)); d /= 10; }

    size_t n = 0;
    out[n++] = digits[0];
    if (prec > 0) {
        out[n++] = '.';
        memcpy(out + n, digits + 1, (size_t)prec);
        n += (size_t)prec;
    }
    out[n++] = upper ? 'E' : 'e';
    out[n++] = (exp < 0) ? '-' : '+';
    int ae = (exp < 0) ? -exp : exp;
    if (ae < 10) out[n++] = '0';
    n += utoa((uint64_t)ae, 10, 0, out + n);
    return n;
}

/* %g: shortest of %e / %f, trailing zeros stripped unless '#'. */
static size_t fmt_gen(char *out, double v, int prec, int upper, int alt) {
    if (prec < 0) prec = 6;
    if (prec == 0) prec = 1;

    int e = dec_exp(v);
    size_t n = (e < -4 || e >= prec) ? fmt_sci(out, v, prec - 1, upper)
                                     : fmt_fixed(out, v, prec - 1 - e);
    out[n] = '\0';
    if (alt) return n;

    char *dot = strchr(out, '.');
    if (!dot) return n;

    /* Trailing zeros are stripped from the mantissa only; any exponent
     * suffix ("e+06") has to be shifted down with it. */
    char *mant_end = out + n;
    for (char *q = out; *q; q++) {
        if (*q == 'e' || *q == 'E') { mant_end = q; break; }
    }

    char *p = mant_end;
    while (p > dot + 1 && p[-1] == '0') p--;
    if (p == dot + 1) p = dot;          /* drop the '.' as well */

    size_t tail = (size_t)((out + n) - mant_end);
    if (tail) memmove(p, mant_end, tail);
    n = (size_t)(p - out) + tail;
    out[n] = '\0';
    return n;
}

/* ── field emission ──────────────────────────────────────────────── */

static void emit_pad(PutFn put, void *ctx, size_t *total, char c, size_t n) {
    char pad[64];
    memset(pad, c, sizeof(pad));
    while (n) {
        size_t k = (n < sizeof(pad)) ? n : sizeof(pad);
        put(ctx, pad, k);
        *total += k;
        n -= k;
    }
}

static void emit_field(PutFn put, void *ctx, size_t *total,
                       const char *prefix, size_t plen,
                       const char *body, size_t blen,
                       int width, int left, int zero) {
    size_t flen = plen + blen;
    size_t pad  = (width > 0 && (size_t)width > flen) ? (size_t)width - flen : 0;

    if (!left && !zero) emit_pad(put, ctx, total, ' ', pad);
    if (plen) { put(ctx, prefix, plen); *total += plen; }
    if (!left && zero)  emit_pad(put, ctx, total, '0', pad);
    if (blen) { put(ctx, body, blen); *total += blen; }
    if (left)  emit_pad(put, ctx, total, ' ', pad);
}

/* ── core formatter ──────────────────────────────────────────────── */

static int ocl_format(PutFn put, void *ctx, const char *fmt, va_list ap) {
    size_t total = 0;
    char   body[128];
    char   prefix[4];

    for (const char *p = fmt; *p; ) {
        if (*p != '%') {
            const char *start = p;
            while (*p && *p != '%') p++;
            size_t n = (size_t)(p - start);
            put(ctx, start, n);
            total += n;
            continue;
        }

        p++;
        if (*p == '%') { put(ctx, "%", 1); total++; p++; continue; }
        if (!*p) break;

        int left = 0, plus = 0, space = 0, zero = 0, alt = 0;
        for (;; p++) {
            if      (*p == '-') left  = 1;
            else if (*p == '+') plus  = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '0') zero  = 1;
            else if (*p == '#') alt   = 1;
            else break;
        }

        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            p++;
            if (width < 0) { left = 1; width = -width; }
        } else {
            while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        }

        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                prec = va_arg(ap, int);
                p++;
                if (prec < 0) prec = -1;
            } else {
                while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
            }
        }

        int len = 0;   /* 0 int, 1 long, 2 long long, 3 size_t, 4 intmax, 5 short, 6 char */
        if (*p == 'h') { p++; len = 5; if (*p == 'h') { p++; len = 6; } }
        else if (*p == 'l') { p++; len = 1; if (*p == 'l') { p++; len = 2; } }
        else if (*p == 'z') { p++; len = 3; }
        else if (*p == 'j') { p++; len = 4; }
        else if (*p == 't') { p++; len = 3; }

        char conv = *p ? *p++ : '\0';

        size_t plen = 0, blen = 0;
        int    numeric = 0;

        switch (conv) {
            case 'd': case 'i': {
                int64_t v;
                switch (len) {
                    case 1: v = va_arg(ap, long); break;
                    case 2: v = va_arg(ap, long long); break;
                    case 3: v = (int64_t)va_arg(ap, size_t); break;
                    case 4: v = (int64_t)va_arg(ap, long long); break;
                    case 5: v = (short)va_arg(ap, int); break;
                    case 6: v = (signed char)va_arg(ap, int); break;
                    default: v = va_arg(ap, int); break;
                }
                uint64_t mag = (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
                if (v < 0) prefix[plen++] = '-';
                else if (plus) prefix[plen++] = '+';
                else if (space) prefix[plen++] = ' ';
                blen = utoa(mag, 10, 0, body);
                numeric = 1;
                break;
            }
            case 'u': case 'o': case 'x': case 'X': {
                uint64_t v;
                switch (len) {
                    case 1: v = va_arg(ap, unsigned long); break;
                    case 2: v = va_arg(ap, unsigned long long); break;
                    case 3: v = (uint64_t)va_arg(ap, size_t); break;
                    case 4: v = (uint64_t)va_arg(ap, unsigned long long); break;
                    case 5: v = (unsigned short)va_arg(ap, unsigned int); break;
                    case 6: v = (unsigned char)va_arg(ap, unsigned int); break;
                    default: v = va_arg(ap, unsigned int); break;
                }
                unsigned base = (conv == 'o') ? 8u : (conv == 'u') ? 10u : 16u;
                int upper = (conv == 'X');
                blen = utoa(v, base, upper, body);
                if (alt && base == 16 && v != 0) {
                    prefix[plen++] = '0';
                    prefix[plen++] = upper ? 'X' : 'x';
                } else if (alt && base == 8 && (blen == 0 || body[0] != '0')) {
                    prefix[plen++] = '0';
                }
                numeric = 1;
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                prefix[plen++] = '0';
                prefix[plen++] = 'x';
                blen = utoa(v, 16, 0, body);
                numeric = 1;
                break;
            }
            case 'c': {
                body[0] = (char)va_arg(ap, int);
                blen = 1;
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t n = strlen(s);
                if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
                emit_field(put, ctx, &total, prefix, 0, s, n, width, left, 0);
                continue;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                double v = va_arg(ap, double);
                int upper = (conv == 'E' || conv == 'G' || conv == 'F');

                if (v != v) {
                    const char *s = upper ? "NAN" : "nan";
                    emit_field(put, ctx, &total, prefix, 0, s, 3, width, left, 0);
                    continue;
                }
                if (v < 0.0 || (v == 0.0 && 1.0 / v < 0.0)) { prefix[plen++] = '-'; v = -v; }
                else if (plus)  prefix[plen++] = '+';
                else if (space) prefix[plen++] = ' ';

                if (v > 1.7976931348623157e308) {
                    const char *s = upper ? "INF" : "inf";
                    emit_field(put, ctx, &total, prefix, plen, s, 3, width, left, 0);
                    continue;
                }

                if (conv == 'f' || conv == 'F') {
                    if (v >= 1e18) blen = fmt_sci(body, v, (prec < 0) ? 6 : prec, upper);
                    else           blen = fmt_fixed(body, v, prec);
                } else if (conv == 'e' || conv == 'E') {
                    blen = fmt_sci(body, v, prec, upper);
                } else {
                    blen = fmt_gen(body, v, prec, upper, alt);
                }
                numeric = 1;
                break;
            }
            case '\0':
                continue;
            default:
                body[0] = '%';
                body[1] = conv;
                blen = 2;
                break;
        }

        emit_field(put, ctx, &total, prefix, plen, body, blen,
                   width, left, zero && numeric && !left);
    }

    return (int)total;
}

/* ── public entry points ─────────────────────────────────────────── */

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    BufSink sink;
    sink.buf = buf;
    sink.cap = n;
    sink.len = 0;

    int r = ocl_format(buf_put, &sink, fmt, ap);
    if (n > 0) {
        size_t end = (sink.len < n) ? sink.len : n - 1;
        buf[end] = '\0';
    }
    return r;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    return ocl_format(file_put, f, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
