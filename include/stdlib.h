#ifndef OCL_STDLIB_H
#define OCL_STDLIB_H

/*
 * stdlib.h — OCL Standard Library
 *
 * Defines every built-in function ID and the public dispatch API.
 * IDs 1-2 are handled inline in vm.c; everything else goes through
 * stdlib_dispatch().
 *
 *  1- 2   I/O core         (vm.c)
 *  3- 4   I/O extended     (stdlib.c)
 * 10-20   Math             (stdlib.c)
 * 30-38   String           (stdlib.c)
 * 40-44   Type conversions (stdlib.c)
 * 50-56   Utilities        (stdlib.c)
 */

#include "common.h"

/* Forward declaration so stdlib.h doesn't need to include vm.h */
struct VM;
typedef struct VM VM;

/* ── Built-in IDs ────────────────────────────────────────────────── */

/* I/O */
#define BUILTIN_PRINT       1
#define BUILTIN_PRINTF      2
#define BUILTIN_INPUT       3
#define BUILTIN_READLINE    4

/* Math */
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

/* String */
#define BUILTIN_STRLEN      30
#define BUILTIN_SUBSTR      31
#define BUILTIN_TOUPPER     32
#define BUILTIN_TOLOWER     33
#define BUILTIN_STRCONTAINS 34
#define BUILTIN_STRINDEXOF  35
#define BUILTIN_STRREPLACE  36
#define BUILTIN_STRTRIM     37
#define BUILTIN_STRSPLIT    38

/* Type conversions */
#define BUILTIN_TO_INT      40
#define BUILTIN_TO_FLOAT    41
#define BUILTIN_TO_STRING   42
#define BUILTIN_TO_BOOL     43
#define BUILTIN_TYPEOF      44

/* Utilities */
#define BUILTIN_EXIT        50
#define BUILTIN_ASSERT      51
#define BUILTIN_IS_NULL     52
#define BUILTIN_IS_INT      53
#define BUILTIN_IS_FLOAT    54
#define BUILTIN_IS_STRING   55
#define BUILTIN_IS_BOOL     56

/* ── Dispatch table entry ─────────────────────────────────────────── */

typedef struct {
    int         id;
    const char *name;
    void      (*fn)(VM *vm, int argc);
} StdlibEntry;

/* ── Public API ───────────────────────────────────────────────────── */

void  stdlib_init(void);
void  stdlib_cleanup(void);

/* Returns true if handled, false if ID unknown */
bool  stdlib_dispatch(VM *vm, int id, int argc);

/* Used by codegen to resolve function name -> builtin ID.
   Returns NULL if name is not a stdlib function. */
const StdlibEntry *stdlib_lookup_by_name(const char *name);

/* Returns the full table and its size (used by codegen to pre-register all builtins) */
const StdlibEntry *stdlib_get_table(size_t *out_count);

#endif /* OCL_STDLIB_H */