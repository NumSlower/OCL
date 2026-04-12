#ifndef OCL_NUMOS_TIME_H
#define OCL_NUMOS_TIME_H

typedef long time_t;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

#define CLOCK_MONOTONIC 1

time_t time(time_t *out);
int clock_gettime(int clock_id, struct timespec *ts);

#endif
