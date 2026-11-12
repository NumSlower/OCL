/*
 * OCL port layer — the only place that talks to the operating system.
 *
 * Everything above this file (allocator, stdio, time, process exit) is
 * written from scratch in src/libc/. Porting OCL to a new host means
 * reimplementing exactly these primitives; nothing else in the tree
 * needs to change.
 */
#ifndef OCL_PORT_H
#define OCL_PORT_H

#include <stddef.h>

/* Byte-oriented file descriptors. Return the byte count, or -1 on error
 * with `errno` set. */
long ocl_sys_read(int fd, void *buf, size_t n);
long ocl_sys_write(int fd, const void *buf, size_t n);

/* flags: OCL_O_RDONLY / OCL_O_WRONLY / OCL_O_RDWR, optionally OR'd with
 * OCL_O_CREAT / OCL_O_TRUNC / OCL_O_APPEND. */
#define OCL_O_RDONLY 0x0000
#define OCL_O_WRONLY 0x0001
#define OCL_O_RDWR   0x0002
#define OCL_O_CREAT  0x0040
#define OCL_O_TRUNC  0x0200
#define OCL_O_APPEND 0x0400

long ocl_sys_open(const char *path, int flags, int mode);
long ocl_sys_close(int fd);
long ocl_sys_lseek(int fd, long offset, int whence);

/* Anonymous read/write memory. Returns NULL on failure. */
void *ocl_sys_map(size_t len);
void  ocl_sys_unmap(void *addr, size_t len);

/* Terminate the process immediately. */
void ocl_sys_exit(int status) __attribute__((noreturn));

/* CLOCK_REALTIME = 0, CLOCK_MONOTONIC = 1. Returns 0 on success. */
int ocl_sys_clock(int clk, long *sec, long *nsec);

/* Monotonic nanoseconds, used by the allocator's bookkeeping only. */
unsigned long long ocl_sys_ticks(void);

#endif /* OCL_PORT_H */
