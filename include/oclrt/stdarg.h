/*
 * OCL runtime stdarg.h — no compiler-supplied header is used.
 *
 * The variadic machinery is a compiler builtin on every supported target;
 * this header only names it.
 */
#ifndef OCL_FS_STDARG_H
#define OCL_FS_STDARG_H

typedef __builtin_va_list va_list;
typedef __builtin_va_list __gnuc_va_list;

#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_copy(dst, src)   __builtin_va_copy(dst, src)

#endif /* OCL_FS_STDARG_H */
