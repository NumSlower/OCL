/*
 * Linux syscall port for OCL's freestanding runtime.
 *
 * No libc is involved: every primitive is a raw system call issued with
 * inline assembly. x86-64 and aarch64 are covered; other targets only
 * need this file rewritten.
 */
#include "port.h"

#if defined(__x86_64__)

static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

#define SYS_READ           0
#define SYS_WRITE          1
#define SYS_OPEN           2
#define SYS_CLOSE          3
#define SYS_LSEEK          8
#define SYS_MMAP           9
#define SYS_MUNMAP        11
#define SYS_EXIT          60
#define SYS_CLOCK_GETTIME 228

long ocl_sys_read(int fd, void *buf, size_t n) {
    return sys6(SYS_READ, fd, (long)buf, (long)n, 0, 0, 0);
}

long ocl_sys_write(int fd, const void *buf, size_t n) {
    return sys6(SYS_WRITE, fd, (long)buf, (long)n, 0, 0, 0);
}

long ocl_sys_open(const char *path, int flags, int mode) {
    return sys6(SYS_OPEN, (long)path, flags, mode, 0, 0, 0);
}

long ocl_sys_close(int fd) { return sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0); }

long ocl_sys_lseek(int fd, long offset, int whence) {
    return sys6(SYS_LSEEK, fd, offset, whence, 0, 0, 0);
}

void *ocl_sys_map(size_t len) {
    /* PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, fd = -1 */
    long r = sys6(SYS_MMAP, 0, (long)len, 3, 0x22, -1, 0);
    if (r < 0 && r > -4096) return NULL;
    return (void *)r;
}

void ocl_sys_unmap(void *addr, size_t len) {
    sys6(SYS_MUNMAP, (long)addr, (long)len, 0, 0, 0, 0);
}

void ocl_sys_exit(int status) {
    for (;;) sys6(SYS_EXIT, status, 0, 0, 0, 0, 0);
}

int ocl_sys_clock(int clk, long *sec, long *nsec) {
    long ts[2] = {0, 0};
    long r = sys6(SYS_CLOCK_GETTIME, clk, (long)ts, 0, 0, 0, 0);
    if (r < 0) { errno = EINVAL; return -1; }
    *sec  = ts[0];
    *nsec = ts[1];
    return 0;
}

unsigned long long ocl_sys_ticks(void) {
    long s = 0, ns = 0;
    if (ocl_sys_clock(1, &s, &ns) != 0) return 0;
    return (unsigned long long)s * 1000000000ULL + (unsigned long long)ns;
}

#elif defined(__aarch64__)

static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                     : "memory");
    return x0;
}

#define SYS_READ           63
#define SYS_WRITE          64
#define SYS_OPENAT         56
#define SYS_CLOSE          57
#define SYS_LSEEK          62
#define SYS_MMAP          222
#define SYS_MUNMAP        215
#define SYS_EXIT           93
#define SYS_CLOCK_GETTIME 113
#define AT_FDCWD          (-100)

long ocl_sys_read(int fd, void *buf, size_t n) {
    return sys6(SYS_READ, fd, (long)buf, (long)n, 0, 0, 0);
}

long ocl_sys_write(int fd, const void *buf, size_t n) {
    return sys6(SYS_WRITE, fd, (long)buf, (long)n, 0, 0, 0);
}

long ocl_sys_open(const char *path, int flags, int mode) {
    return sys6(SYS_OPENAT, AT_FDCWD, (long)path, flags, mode, 0, 0);
}

long ocl_sys_close(int fd) { return sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0); }

long ocl_sys_lseek(int fd, long offset, int whence) {
    return sys6(SYS_LSEEK, fd, offset, whence, 0, 0, 0);
}

void *ocl_sys_map(size_t len) {
    long r = sys6(SYS_MMAP, 0, (long)len, 3, 0x22, -1, 0);
    if (r < 0 && r > -4096) return NULL;
    return (void *)r;
}

void ocl_sys_unmap(void *addr, size_t len) {
    sys6(SYS_MUNMAP, (long)addr, (long)len, 0, 0, 0, 0);
}

void ocl_sys_exit(int status) {
    for (;;) sys6(SYS_EXIT, status, 0, 0, 0, 0, 0);
}

int ocl_sys_clock(int clk, long *sec, long *nsec) {
    long ts[2] = {0, 0};
    long r = sys6(SYS_CLOCK_GETTIME, clk, (long)ts, 0, 0, 0, 0);
    if (r < 0) { errno = EINVAL; return -1; }
    *sec  = ts[0];
    *nsec = ts[1];
    return 0;
}

unsigned long long ocl_sys_ticks(void) {
    long s = 0, ns = 0;
    if (ocl_sys_clock(1, &s, &ns) != 0) return 0;
    return (unsigned long long)s * 1000000000ULL + (unsigned long long)ns;
}

#else
#  error "OCL freestanding port: unsupported architecture (add it to src/libc/port_linux.c)"
#endif
