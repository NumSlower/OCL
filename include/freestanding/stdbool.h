/*
 * OCL freestanding stdbool.h — no compiler-supplied header is used.
 */
#ifndef OCL_FS_STDBOOL_H
#define OCL_FS_STDBOOL_H

#ifndef __cplusplus
#  define bool  _Bool
#  define true  1
#  define false 0
#endif

#define __bool_true_false_are_defined 1

#endif /* OCL_FS_STDBOOL_H */
