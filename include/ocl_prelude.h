/*
 * ocl_prelude.h — force-included into every translation unit (-include).
 *
 * The build compiles with -nostdinc and -Iinclude, and every runtime header
 * is included by its in-tree path (<oclrt/...>), so nothing here can resolve
 * to a host header. Nothing comes from the system or from a third party.
 *
 * Force-including them guarantees the runtime declarations are visible even
 * if an individual source file's own #include lines are edited or removed,
 * so the build cannot regress into "unknown type name 'bool'" errors.
 */
#ifndef OCL_PRELUDE_H
#define OCL_PRELUDE_H

#include <oclrt/stddef.h>
#include <oclrt/stdint.h>
#include <oclrt/stdbool.h>
#include <oclrt/stdarg.h>

#include <oclrt/stdio.h>
#include <oclrt/stdlib.h>
#include <oclrt/string.h>
#include <oclrt/ctype.h>
#include <oclrt/math.h>
#include <oclrt/time.h>
#include <oclrt/errno.h>
#include <oclrt/inttypes.h>

#endif /* OCL_PRELUDE_H */
