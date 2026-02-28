#ifndef OCL_COMMON_H
#define OCL_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef struct {
    int         line;
    int         column;
    const char *filename;
} SourceLocation;

typedef enum {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_CHAR,
    VALUE_NULL
} ValueType;

typedef struct {
    ValueType type;
    /*
     * owned — only meaningful when type == VALUE_STRING.
     *
     * true  : this Value owns its string_val allocation and value_free()
     *         will call free() on it.
     * false : this Value is a *borrowed* view of a string owned by
     *         something else (the Bytecode constant pool, or a local/global
     *         slot).  value_free() is a no-op for borrowed strings.
     *
     * Rules:
     *   - value_string(ptr)       — takes ownership  (owned = true)
     *   - value_string_copy(cstr) — allocates + owns (owned = true)
     *   - value_string_borrow(ptr)— borrows          (owned = false)
     *   - value_own_copy(v)       — ensures v is owned, copying if needed
     *
     * This lets OP_PUSH_CONST push a borrowed view into the constant pool
     * (zero allocation), while OP_STORE_VAR/OP_STORE_GLOBAL call
     * value_own_copy() to ensure the stored value owns its string.
     */
    bool owned;
    union {
        int64_t  int_val;
        double   float_val;
        char    *string_val;
        bool     bool_val;
        char     char_val;
    } data;
} Value;

/* ── Constructors ─────────────────────────────────────────────────── */

/* Non-string constructors (owned flag irrelevant, set false for clarity) */
static inline Value value_int(int64_t i)  { return (Value){VALUE_INT,   false, {.int_val   = i}}; }
static inline Value value_float(double f) { return (Value){VALUE_FLOAT, false, {.float_val = f}}; }
static inline Value value_bool(bool b)    { return (Value){VALUE_BOOL,  false, {.bool_val  = b}}; }
static inline Value value_char(char c)    { return (Value){VALUE_CHAR,  false, {.char_val  = c}}; }
static inline Value value_null(void)      { return (Value){VALUE_NULL,  false, {.int_val   = 0}}; }

/* Takes ownership of an already-heap-allocated string. */
static inline Value value_string(char *s) {
    return (Value){VALUE_STRING, true, {.string_val = s}};
}

/* Allocates a fresh copy — result is owned. */
Value  value_string_copy(const char *s);

/* Borrows a string pointer without copying — caller must ensure lifetime. */
static inline Value value_string_borrow(const char *s) {
    return (Value){VALUE_STRING, false, {.string_val = (char *)s}};
}

/*
 * Returns an owned copy of v.
 * - If v is already an owned string, returns v unchanged (no extra alloc).
 * - If v is a borrowed string, allocates and returns an owned copy.
 * - For non-strings, returns v unchanged (owned flag is irrelevant).
 */
Value value_own_copy(Value v);

/* ── Utilities ────────────────────────────────────────────────────── */

bool   value_is_truthy(Value v);

/*
 * Free a value's resources.
 * For strings: only frees if owned == true.
 * For all other types: no-op (no heap allocation).
 */
void   value_free(Value v);

char  *value_to_string(Value v);

/* ── Memory helpers ───────────────────────────────────────────────── */
void  *ocl_malloc(size_t size);
void  *ocl_realloc(void *ptr, size_t size);
void   ocl_free(void *ptr);
char  *ocl_strdup(const char *s);

#endif