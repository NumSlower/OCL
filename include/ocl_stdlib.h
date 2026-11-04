#ifndef OCL_STDLIB_H
#define OCL_STDLIB_H
#include "common.h"
struct VM; typedef struct VM VM;
#define BUILTIN_PRINT       1
#define BUILTIN_PRINTF      2
#define BUILTIN_INPUT       3
#define BUILTIN_READLINE    4
/* Math builtins */
#define BUILTIN_ABS         10
#define BUILTIN_SQRT        11
#define BUILTIN_POW         12
#define BUILTIN_SIN         13
#define BUILTIN_COS         14
#define BUILTIN_TAN         15
#define BUILTIN_FLOOR       16
#define BUILTIN_CEIL        17
#define BUILTIN_ROUND       18
#define BUILTIN_MAX         19
#define BUILTIN_MIN         20
#define BUILTIN_STRLEN      30
#define BUILTIN_SUBSTR      31
#define BUILTIN_TOUPPER     32
#define BUILTIN_TOLOWER     33
#define BUILTIN_STRCONTAINS 34
#define BUILTIN_STRINDEXOF  35
#define BUILTIN_STRREPLACE  36
#define BUILTIN_STRTRIM     37
#define BUILTIN_STRSPLIT    38
/* Type conversion builtins */
#define BUILTIN_TOINT       40
#define BUILTIN_TOFLOAT     41
#define BUILTIN_TOSTRING    42
#define BUILTIN_TOBOOL      43
#define BUILTIN_TYPEOF      44
/* Inspection builtins */
#define BUILTIN_ISNULL      45
#define BUILTIN_ISINT       46
#define BUILTIN_ISFLOAT     47
#define BUILTIN_ISSTRING    48
#define BUILTIN_ISBOOL      49
#define BUILTIN_EXIT        50
#define BUILTIN_ASSERT      51
/* Array builtins */
#define BUILTIN_ARRAY_NEW   60
#define BUILTIN_ARRAY_PUSH  61
#define BUILTIN_ARRAY_POP   62
#define BUILTIN_ARRAY_GET   63
#define BUILTIN_ARRAY_SET   64
#define BUILTIN_ARRAY_LEN   65
#define BUILTIN_TIMENOW     66
#define BUILTIN_RANDOM      67

typedef struct { int id; const char *name; void (*fn)(VM *vm, int argc); } StdlibEntry;
bool               stdlib_dispatch(VM *vm, int id, int argc);
const StdlibEntry *stdlib_lookup_by_name(const char *name);
const StdlibEntry *stdlib_get_table(size_t *out_count);
#endif