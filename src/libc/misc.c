/*
 * OCL freestanding ctype, PRNG, clock and errno support.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "port.h"

int errno = 0;

int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isalpha(int c)  { return isupper(c) || islower(c); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
int isprint(int c) { return c >= 0x20 && c <= 0x7e; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

/* ── PRNG: 64-bit LCG, top 31 bits (deterministic, no host rand) ──── */

static unsigned long long g_rand_state = 0x2545F4914F6CDD1DULL;

void srand(unsigned int seed) {
    g_rand_state = 0x2545F4914F6CDD1DULL ^ ((unsigned long long)seed * 6364136223846793005ULL);
    if (g_rand_state == 0) g_rand_state = 1;
}

int rand(void) {
    g_rand_state = g_rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((g_rand_state >> 33) & 0x7fffffffULL);
}

/* ── time ─────────────────────────────────────────────────────────── */

int clock_gettime(clockid_t clk, struct timespec *ts) {
    long s = 0, ns = 0;
    if (!ts) { errno = EINVAL; return -1; }
    if (ocl_sys_clock((int)clk, &s, &ns) != 0) return -1;
    ts->tv_sec  = s;
    ts->tv_nsec = ns;
    return 0;
}

time_t time(time_t *out) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        if (out) *out = (time_t)-1;
        return (time_t)-1;
    }
    if (out) *out = (time_t)ts.tv_sec;
    return (time_t)ts.tv_sec;
}

/* ── strerror ─────────────────────────────────────────────────────── */

char *strerror(int errnum) {
    switch (errnum) {
        case 0:      return (char *)"Success";
        case EPERM:  return (char *)"Operation not permitted";
        case ENOENT: return (char *)"No such file or directory";
        case EINTR:  return (char *)"Interrupted system call";
        case EIO:    return (char *)"Input/output error";
        case EBADF:  return (char *)"Bad file descriptor";
        case EAGAIN: return (char *)"Resource temporarily unavailable";
        case ENOMEM: return (char *)"Cannot allocate memory";
        case EACCES: return (char *)"Permission denied";
        case EFAULT: return (char *)"Bad address";
        case EEXIST: return (char *)"File exists";
        case EINVAL: return (char *)"Invalid argument";
        case ENOSPC: return (char *)"No space left on device";
        case EPIPE:  return (char *)"Broken pipe";
        case ERANGE: return (char *)"Numerical result out of range";
        case ENOSYS: return (char *)"Function not implemented";
        default:     return (char *)"Unknown error";
    }
}
