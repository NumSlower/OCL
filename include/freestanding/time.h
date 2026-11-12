/*
 * OCL freestanding time.h — implemented in src/libc/misc.c.
 */
#ifndef OCL_FS_TIME_H
#define OCL_FS_TIME_H

typedef long time_t;
typedef int  clockid_t;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int  clock_gettime(clockid_t clk, struct timespec *ts);
time_t time(time_t *out);

#endif /* OCL_FS_TIME_H */
