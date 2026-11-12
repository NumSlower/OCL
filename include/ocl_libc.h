/*
 * ocl_libc.h — the single entry point for the C runtime surface OCL uses.
 *
 * No OCL source file includes a C standard header directly. Everything goes
 * through this header, which makes the choice of runtime explicit and
 * auditable in one place:
 *
 *   host build          `make`               -> the system headers
 *   freestanding build  `make freestanding`  -> include/freestanding/ headers,
 *                                               implemented in src/libc/
 *
 * The freestanding build compiles with -nostdinc, so the system headers are
 * not even on the search path; the shadow headers in include/freestanding/
 * are the only ones visible. `make check-headers` enforces that no other
 * file reintroduces a direct host include.
 */
#ifndef OCL_LIBC_H
#define OCL_LIBC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#endif /* OCL_LIBC_H */
