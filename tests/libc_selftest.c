/*
 * Self-test for OCL's freestanding C runtime (src/libc/).
 *
 * Built and run by `make check-libc` with -nostdlib, so it exercises the
 * from-scratch allocator, string/memory routines, printf family, number
 * parsing and math library without any host libc present.
 */
#include <oclrt/stdio.h>
#include <oclrt/stdlib.h>
#include <oclrt/string.h>
#include <oclrt/math.h>
#include <oclrt/ctype.h>
#include <oclrt/stdint.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(int ok, const char *what) {
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}

static void check_str(const char *got, const char *want, const char *what) {
    if (got && want && strcmp(got, want) == 0) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL  %s: got \"%s\" want \"%s\"\n",
               what, got ? got : "(null)", want ? want : "(null)");
    }
}

static void check_near(double got, double want, double eps, const char *what) {
    double d = got - want;
    if (d < 0) d = -d;
    if (d <= eps) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL  %s: got %.17g want %.17g\n", what, got, want);
    }
}

int main(void) {
    char buf[256];

    /* ── integers ─────────────────────────────────────────────────── */
    snprintf(buf, sizeof buf, "%d", 42);            check_str(buf, "42", "%d");
    snprintf(buf, sizeof buf, "%d", -42);           check_str(buf, "-42", "%d negative");
    snprintf(buf, sizeof buf, "%i", 0);             check_str(buf, "0", "%i zero");
    snprintf(buf, sizeof buf, "%u", 3000000000u);   check_str(buf, "3000000000", "%u");
    snprintf(buf, sizeof buf, "%x", 255u);          check_str(buf, "ff", "%x");
    snprintf(buf, sizeof buf, "%X", 255u);          check_str(buf, "FF", "%X");
    snprintf(buf, sizeof buf, "%#x", 255u);         check_str(buf, "0xff", "%#x");
    snprintf(buf, sizeof buf, "%o", 8u);            check_str(buf, "10", "%o");
    snprintf(buf, sizeof buf, "%zu", (size_t)123);  check_str(buf, "123", "%zu");
    snprintf(buf, sizeof buf, "%lld", -9000000000LL); check_str(buf, "-9000000000", "%lld");
    snprintf(buf, sizeof buf, "%ld", -7L);          check_str(buf, "-7", "%ld");
    snprintf(buf, sizeof buf, "%hd", (short)-3);    check_str(buf, "-3", "%hd");

    /* ── flags and width ──────────────────────────────────────────── */
    snprintf(buf, sizeof buf, "%-6d|", 7);          check_str(buf, "7     |", "%-6d");
    snprintf(buf, sizeof buf, "%06d", 7);           check_str(buf, "000007", "%06d");
    snprintf(buf, sizeof buf, "%+d", 7);            check_str(buf, "+7", "%+d");
    snprintf(buf, sizeof buf, "% d", 7);            check_str(buf, " 7", "% d");
    snprintf(buf, sizeof buf, "%5.2s", "abcdef");   check_str(buf, "   ab", "%5.2s");
    snprintf(buf, sizeof buf, "%-5.2s|", "abcdef"); check_str(buf, "ab   |", "%-5.2s");
    snprintf(buf, sizeof buf, "%c", 'A');           check_str(buf, "A", "%c");
    snprintf(buf, sizeof buf, "%%");                check_str(buf, "%", "%%");
    snprintf(buf, sizeof buf, "%s", (char *)NULL);  check_str(buf, "(null)", "%s NULL");

    /* ── truncation contract ──────────────────────────────────────── */
    memset(buf, 'X', sizeof buf);
    int n = snprintf(buf, 4, "%s", "abcdef");
    check(n == 6, "snprintf returns full length");
    check_str(buf, "abc", "snprintf truncates and NUL-terminates");

    /* ── floating point ───────────────────────────────────────────── */
    snprintf(buf, sizeof buf, "%f", 3.5);           check_str(buf, "3.500000", "%f");
    snprintf(buf, sizeof buf, "%.3f", 3.14159);     check_str(buf, "3.142", "%.3f");
    snprintf(buf, sizeof buf, "%.0f", 2.5);         check_str(buf, "3", "%.0f rounding");
    snprintf(buf, sizeof buf, "%08.3f", 3.14159);   check_str(buf, "0003.142", "%08.3f");
    snprintf(buf, sizeof buf, "%e", 1234.5);        check_str(buf, "1.234500e+03", "%e");
    snprintf(buf, sizeof buf, "%.2e", 0.00012345);  check_str(buf, "1.23e-04", "%.2e");
    snprintf(buf, sizeof buf, "%g", 0.5);           check_str(buf, "0.5", "%g 0.5");
    snprintf(buf, sizeof buf, "%g", 100000.0);      check_str(buf, "100000", "%g 1e5");
    snprintf(buf, sizeof buf, "%g", 1000000.0);     check_str(buf, "1e+06", "%g 1e6");
    snprintf(buf, sizeof buf, "%g", 0.0001);        check_str(buf, "0.0001", "%g 1e-4");
    snprintf(buf, sizeof buf, "%g", 0.00001);       check_str(buf, "1e-05", "%g 1e-5");
    snprintf(buf, sizeof buf, "%f", -0.0);          check_str(buf, "-0.000000", "%f negative zero");
    snprintf(buf, sizeof buf, "%f", (double)INFINITY); check_str(buf, "inf", "%f inf");
    snprintf(buf, sizeof buf, "%f", (double)NAN);   check_str(buf, "nan", "%f nan");

    /* ── string and memory ────────────────────────────────────────── */
    check(strlen("hello") == 5, "strlen");
    check(strcmp("abc", "abd") < 0, "strcmp");
    check(strncmp("abcdef", "abcxyz", 3) == 0, "strncmp");
    check(strchr("hello", 'l') != NULL && *strchr("hello", 'l') == 'l', "strchr");
    check(strrchr("hello", 'l') != NULL && *strrchr("hello", 'l') == 'l', "strrchr");
    check(strstr("hello world", "world") != NULL, "strstr");
    check(strstr("hello", "zzz") == NULL, "strstr miss");

    char ov[8] = "abcdef";
    memmove(ov + 1, ov, 5);
    check_str(ov, "aabcde", "memmove overlap forward");
    memmove(ov, ov + 1, 5);
    check_str(ov, "abcdee", "memmove overlap backward");

    char z[4];
    memset(z, 'q', 4);
    check(z[0] == 'q' && z[3] == 'q', "memset");
    check(memcmp("abc", "abc", 3) == 0, "memcmp equal");
    check(memcmp("abc", "abd", 3) < 0, "memcmp less");

    char cat[16];
    strcpy(cat, "ab");
    strcat(cat, "cd");
    check_str(cat, "abcd", "strcpy/strcat");

    /* ── parsing ──────────────────────────────────────────────────── */
    check(strtol("-42", NULL, 10) == -42, "strtol");
    check(strtol("0x1f", NULL, 0) == 31, "strtol hex");
    check(strtoll("9000000000", NULL, 10) == 9000000000LL, "strtoll");
    check_near(strtod("3.25", NULL), 3.25, 1e-15, "strtod");
    check_near(strtod("-1.5e3", NULL), -1500.0, 1e-9, "strtod exponent");
    check(atoi("123") == 123, "atoi");

    /* ── allocator ────────────────────────────────────────────────── */
    int *arr = (int *)calloc(16, sizeof(int));
    check(arr != NULL, "calloc returns memory");
    check(arr && arr[0] == 0 && arr[15] == 0, "calloc zeroes");
    if (arr) {
        for (int i = 0; i < 16; i++) arr[i] = i;
        int *big = (int *)realloc(arr, 64 * sizeof(int));
        check(big != NULL, "realloc grows");
        check(big && big[0] == 0 && big[15] == 15, "realloc preserves contents");
        free(big);
    }
    void *p = malloc(0);
    check(p == NULL, "malloc(0) returns NULL");
    free(NULL);   /* must not crash */
    g_pass++;

    /* ── math ─────────────────────────────────────────────────────── */
    check_near(sqrt(2.0), 1.4142135623730951, 1e-15, "sqrt(2)");
    check_near(sqrt(144.0), 12.0, 1e-15, "sqrt(144)");
    check_near(pow(2.0, 10.0), 1024.0, 1e-12, "pow(2,10)");
    check_near(pow(2.0, -2.0), 0.25, 1e-15, "pow(2,-2)");
    check_near(exp(0.0), 1.0, 1e-15, "exp(0)");
    check_near(exp(1.0), 2.718281828459045, 1e-14, "exp(1)");
    check_near(log(1.0), 0.0, 1e-15, "log(1)");
    check_near(log(2.718281828459045), 1.0, 1e-14, "log(e)");
    check_near(log10(1000.0), 3.0, 1e-14, "log10(1000)");
    check_near(sin(0.0), 0.0, 1e-15, "sin(0)");
    check_near(cos(0.0), 1.0, 1e-15, "cos(0)");
    check_near(sin(3.141592653589793 / 2.0), 1.0, 1e-14, "sin(pi/2)");
    check_near(cos(3.141592653589793), -1.0, 1e-14, "cos(pi)");
    check_near(tan(0.7853981633974483), 1.0, 1e-13, "tan(pi/4)");
    check_near(atan(1.0), 0.7853981633974483, 1e-14, "atan(1)");
    check_near(asin(1.0), 1.5707963267948966, 1e-14, "asin(1)");
    check_near(acos(0.0), 1.5707963267948966, 1e-14, "acos(0)");
    check_near(floor(-3.5), -4.0, 0.0, "floor(-3.5)");
    check_near(ceil(-3.5), -3.0, 0.0, "ceil(-3.5)");
    check_near(round(3.5), 4.0, 0.0, "round(3.5)");
    check_near(round(-3.5), -4.0, 0.0, "round(-3.5)");
    check_near(trunc(-3.9), -3.0, 0.0, "trunc(-3.9)");
    check_near(fmod(10.0, 3.0), 1.0, 1e-15, "fmod(10,3)");
    check_near(fabs(-2.5), 2.5, 0.0, "fabs");
    check_near(ldexp(1.0, 10), 1024.0, 0.0, "ldexp");

    /* ── ctype ────────────────────────────────────────────────────── */
    check(isdigit('7') && !isdigit('x'), "isdigit");
    check(isspace(' ') && isspace('\n'), "isspace");
    check(toupper('a') == 'A' && tolower('A') == 'a', "toupper/tolower");

    printf("libc selftest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
