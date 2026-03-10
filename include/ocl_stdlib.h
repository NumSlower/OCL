#ifndef OCL_STDLIB_H
#define OCL_STDLIB_H
#include "common.h"
struct VM; typedef struct VM VM;
#define BUILTIN_PRINT       1
#define BUILTIN_PRINTF      2
#define BUILTIN_INPUT       3
#define BUILTIN_READLINE    4
/* Math builtins removed: abs, sqrt, pow, sin, cos, tan, floor, ceil, round, max, min */
#define BUILTIN_STRLEN      30
#define BUILTIN_SUBSTR      31
#define BUILTIN_TOUPPER     32
#define BUILTIN_TOLOWER     33
#define BUILTIN_STRCONTAINS 34
#define BUILTIN_STRINDEXOF  35
#define BUILTIN_STRREPLACE  36
#define BUILTIN_STRTRIM     37
#define BUILTIN_STRSPLIT    38
/* Type conversion builtins removed: toInt, toFloat, toString, toBool, typeOf */
/* Inspection builtins removed: isNull, isInt, isFloat, isString, isBool */
#define BUILTIN_EXIT        50
/* assert removed */
/* Array builtins */
#define BUILTIN_ARRAY_NEW   60
#define BUILTIN_ARRAY_PUSH  61
#define BUILTIN_ARRAY_POP   62
#define BUILTIN_ARRAY_GET   63
#define BUILTIN_ARRAY_SET   64
#define BUILTIN_ARRAY_LEN   65
/* timeNow removed */
#define BUILTIN_RANDOM      67

typedef struct { int id; const char *name; void (*fn)(VM *vm, int argc); } StdlibEntry;
bool               stdlib_dispatch(VM *vm, int id, int argc);
const StdlibEntry *stdlib_lookup_by_name(const char *name);
const StdlibEntry *stdlib_get_table(size_t *out_count);
#endif