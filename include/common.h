#ifndef OCL_COMMON_H
#define OCL_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ── Compiler hints ───────────────────────────────────────────────── */

#if defined(__GNUC__) || defined(__clang__)
#  define OCL_PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#  define OCL_NORETURN               __attribute__((noreturn))
#  define OCL_UNUSED                 __attribute__((unused))
#else
#  define OCL_PRINTF_LIKE(fmt, args)
#  define OCL_NORETURN
#  define OCL_UNUSED
#endif

/* ── Source location ──────────────────────────────────────────────── */

/*
 * Carried by every AST node and bytecode instruction so that runtime
 * and compile-time errors can report file, line, and column.
 */
typedef struct {
    int         line;
    int         column;
    const char *filename; /* points into the token/source buffer — not heap-owned */
} SourceLocation;

#define LOC_NONE ((SourceLocation){0, 0, NULL})

/* ── Value types ──────────────────────────────────────────────────── */

typedef enum {
    VALUE_INT,    /* 64-bit signed integer                          */
    VALUE_FLOAT,  /* 64-bit IEEE 754 double                         */
    VALUE_STRING, /* UTF-8 string; may be heap-owned or borrowed    */
    VALUE_BOOL,   /* true / false                                   */
    VALUE_CHAR,   /* single character                               */
    VALUE_ARRAY,  /* reference-counted heap array (OclArray)        */
    VALUE_NULL    /* the null value                                 */
} ValueType;

typedef struct OclArray OclArray;

/*
 * Value — a tagged union representing any OCL runtime value.
 *
 * The `owned` flag controls string and array lifetime:
 *   owned=true  → this Value is responsible for freeing the pointer
 *   owned=false → borrowed; the pointer is owned by something else
 *                 (e.g. a constant in the bytecode pool, or a frame slot)
 *
 * For VALUE_ARRAY, the refcount in OclArray is the ownership mechanism;
 * `owned=true` means this Value holds a reference that must be released.
 */
typedef struct {
    ValueType type;
    bool      owned;
    union {
        int64_t   int_val;
        double    float_val;
        char     *string_val;
        bool      bool_val;
        char      char_val;
        OclArray *array_val;
    } data;
} Value;

/* ── Array heap object ────────────────────────────────────────────── */

/*
 * OclArray — a reference-counted, dynamically-sized array of Values.
 * Elements are stored as Value (so arrays can be heterogeneous).
 */
struct OclArray {
    Value  *elements;
    size_t  length;    /* number of live elements  */
    size_t  capacity;  /* allocated element slots  */
    int     refcount;  /* number of owning Values  */
};

OclArray *ocl_array_new(size_t initial_cap);
void      ocl_array_retain(OclArray *a);   /* increment refcount */
void      ocl_array_release(OclArray *a);  /* decrement refcount; free if zero */
void      ocl_array_set(OclArray *a, size_t idx, Value v);
Value     ocl_array_get(OclArray *a, size_t idx);
void      ocl_array_push(OclArray *a, Value v);

/* ── Value constructors ───────────────────────────────────────────── */

/* Inline constructors for the common scalar types. */
static inline Value value_int(int64_t i)  { return (Value){VALUE_INT,   false, {.int_val   = i}}; }
static inline Value value_float(double f) { return (Value){VALUE_FLOAT, false, {.float_val = f}}; }
static inline Value value_bool(bool b)    { return (Value){VALUE_BOOL,  false, {.bool_val  = b}}; }
static inline Value value_char(char c)    { return (Value){VALUE_CHAR,  false, {.char_val  = c}}; }
static inline Value value_null(void)      { return (Value){VALUE_NULL,  false, {.int_val   = 0}}; }

/* Takes ownership of a heap-allocated array `a`. Increments its refcount. */
static inline Value value_array(OclArray *a) {
    if (a) ocl_array_retain(a);
    return (Value){VALUE_ARRAY, true, {.array_val = a}};
}

/* Takes ownership of heap-allocated string `s` (caller must not free it). */
static inline Value value_string(char *s) {
    return (Value){VALUE_STRING, true, {.string_val = s}};
}

/* Borrows a string pointer — the caller retains ownership. */
static inline Value value_string_borrow(const char *s) {
    return (Value){VALUE_STRING, false, {.string_val = (char *)s}};
}

Value value_string_copy(const char *s); /* strdup + owned */
Value value_own_copy(Value v);          /* deep copy if needed (for storing) */

/* ── Value utilities ──────────────────────────────────────────────── */

/*
 * value_is_truthy — OCL truthiness rules:
 *   Bool   → its value
 *   Int    → non-zero
 *   Float  → non-zero
 *   String → non-empty
 *   Char   → non-NUL
 *   Array  → non-empty
 *   Null   → always false
 */
bool        value_is_truthy(Value v);
void        value_free(Value v);
const char *value_type_name(ValueType t);

/*
 * value_to_string — returns a pointer into a rotating pool of static buffers
 * (OCL_VTOS_POOL_COUNT slots × OCL_VTOS_BUF_SIZE bytes each).
 *
 * Rules for callers:
 *   - Do NOT free the returned pointer.
 *   - Assume the buffer is overwritten after OCL_VTOS_POOL_COUNT further calls.
 *   - strdup() the result if it needs to survive longer than that.
 *
 * The pool allows safe nesting up to 8 levels deep (e.g. arrays of arrays).
 */
#define OCL_VTOS_POOL_COUNT 8
#define OCL_VTOS_BUF_SIZE   8192

char *value_to_string(Value v);

/* ── Memory helpers ───────────────────────────────────────────────── */

/* All of the following abort on OOM rather than returning NULL. */
void  *ocl_malloc(size_t size);
void  *ocl_realloc(void *ptr, size_t size);
void   ocl_free(void *ptr);
char  *ocl_strdup(const char *s);

/* Free a pointer and zero it. */
#define OCL_FREE(p) do { ocl_free(p); (p) = NULL; } while (0)

/*
 * OCL_GROW_ARRAY — double `*cap` until it exceeds `needed`, then realloc `*buf`.
 * Use this whenever a dynamic array needs to grow by one element.
 */
#define OCL_GROW_ARRAY(buf, cap, needed, type) do {        \
    if ((needed) >= *(cap)) {                              \
        size_t _nc = *(cap) ? *(cap) * 2 : 8;             \
        while (_nc <= (needed)) _nc *= 2;                  \
        *(buf) = ocl_realloc(*(buf), _nc * sizeof(type));  \
        *(cap) = _nc;                                      \
    }                                                      \
} while (0)

#endif /* OCL_COMMON_H */