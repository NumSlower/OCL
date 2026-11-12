/*
 * ocl_prelude.h — force-included into every translation unit (-include).
 *
 * The build compiles with -nostdinc and -Iinclude/freestanding, so each of
 * these standard-named headers resolves to the in-tree runtime header in
 * include/freestanding/, never to a host header. Nothing here comes from the
 * system or from a third party.
 *
 * Force-including them guarantees the runtime declarations are visible even
 * if an individual source file's own #include lines are edited or removed,
 * so the build cannot regress into "unknown type name 'bool'" errors.
 */
#ifndef OCL_PRELUDE_H
#define OCL_PRELUDE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#endif /* OCL_PRELUDE_H */
