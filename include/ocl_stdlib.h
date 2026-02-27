#ifndef OCL_STDLIB_H
#define OCL_STDLIB_H
#include "common.h"
struct VM; typedef struct VM VM;
#define BUILTIN_PRINT       1
#define BUILTIN_PRINTF      2
#define BUILTIN_INPUT       3
#define BUILTIN_READLINE    4
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
#define BUILTIN_TO_INT      40
#define BUILTIN_TO_FLOAT    41
#define BUILTIN_TO_STRING   42
#define BUILTIN_TO_BOOL     43
#define BUILTIN_TYPEOF      44
#define BUILTIN_EXIT        50
#define BUILTIN_ASSERT      51
#define BUILTIN_IS_NULL     52
#define BUILTIN_IS_INT      53
#define BUILTIN_IS_FLOAT    54
#define BUILTIN_IS_STRING   55
#define BUILTIN_IS_BOOL     56
typedef struct { int id; const char *name; void (*fn)(VM *vm, int argc); } StdlibEntry;
void  stdlib_init(void);
void  stdlib_cleanup(void);
bool  stdlib_dispatch(VM *vm, int id, int argc);
const StdlibEntry *stdlib_lookup_by_name(const char *name);
const StdlibEntry *stdlib_get_table(size_t *out_count);
#endif
