/*
 * OCL freestanding inttypes.h — format macros for the fixed-width types.
 */
#ifndef OCL_FS_INTTYPES_H
#define OCL_FS_INTTYPES_H

#include <stdint.h>

#if __SIZEOF_LONG__ == 8
#  define __OCL_PRI64 "l"
#else
#  define __OCL_PRI64 "ll"
#endif

#define PRId64 __OCL_PRI64 "d"
#define PRIi64 __OCL_PRI64 "i"
#define PRIu64 __OCL_PRI64 "u"
#define PRIx64 __OCL_PRI64 "x"
#define PRIX64 __OCL_PRI64 "X"
#define PRIo64 __OCL_PRI64 "o"

#define PRId32 "d"
#define PRIu32 "u"
#define PRIx32 "x"

#endif /* OCL_FS_INTTYPES_H */
