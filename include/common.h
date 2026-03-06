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
typedef struct {
    int         line;
    int         column;
    const char *filename;
} SourceLocation;

#define LOC_NONE ((SourceLocation){0, 0, NULL})

/* ── Value types ──────────────────────────────────────────────────── */
typedef enum {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_CHAR,
    VALUE_ARRAY,
    VALUE_NULL
} ValueType;

typedef struct OclArray OclArray;

typedef struct {
    ValueType type;
    bool      owned;   /* true → this Value owns its heap data */
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
struct OclArray {
    Value  *elements;
    size_t  length;
    size_t  capacity;
    int     refcount;
};

OclArray *ocl_array_new(size_t initial_cap);
void      ocl_array_retain(OclArray *a);
void      ocl_array_release(OclArray *a);
void      ocl_array_set(OclArray *a, size_t idx, Value v);
Value     ocl_array_get(OclArray *a, size_t idx);
void      ocl_array_push(OclArray *a, Value v);

/* ── Value constructors (inline for speed) ────────────────────────── */
static inline Value value_int(int64_t i)  { return (Value){VALUE_INT,   false, {.int_val   = i}}; }
static inline Value value_float(double f) { return (Value){VALUE_FLOAT, false, {.float_val = f}}; }
static inline Value value_bool(bool b)    { return (Value){VALUE_BOOL,  false, {.bool_val  = b}}; }
static inline Value value_char(char c)    { return (Value){VALUE_CHAR,  false, {.char_val  = c}}; }
static inline Value value_null(void)      { return (Value){VALUE_NULL,  false, {.int_val   = 0}}; }

static inline Value value_array(OclArray *a) {
    if (a) ocl_array_retain(a);
    return (Value){VALUE_ARRAY, true, {.array_val = a}};
}

/* Takes ownership of heap-allocated string `s`. */
static inline Value value_string(char *s) {
    return (Value){VALUE_STRING, true, {.string_val = s}};
}

/* Borrows a string pointer — caller keeps ownership. */
static inline Value value_string_borrow(const char *s) {
    return (Value){VALUE_STRING, false, {.string_val = (char *)s}};
}

Value value_string_copy(const char *s); /* strdup + owned */
Value value_own_copy(Value v);          /* deep copy if needed */

/* ── Value utilities ──────────────────────────────────────────────── */
bool        value_is_truthy(Value v);
void        value_free(Value v);
const char *value_type_name(ValueType t);

/*
 * value_to_string — returns a pointer into a rotating pool of static
 * buffers (OCL_VTOS_POOL_COUNT slots × OCL_VTOS_BUF_SIZE bytes each).
 *
 * Rules for callers:
 *   - Do NOT free the returned pointer.
 *   - Assume the buffer is overwritten after OCL_VTOS_POOL_COUNT further calls.
 *   - strdup() the result if you need it to survive longer.
 */
#define OCL_VTOS_POOL_COUNT  8
#define OCL_VTOS_BUF_SIZE    8192

char *value_to_string(Value v);

/* ── Memory helpers (abort on OOM) ───────────────────────────────── */
void  *ocl_malloc(size_t size);
void  *ocl_realloc(void *ptr, size_t size);
void   ocl_free(void *ptr);
char  *ocl_strdup(const char *s);

/* Convenience: free and zero a pointer. */
#define OCL_FREE(p) do { ocl_free(p); (p) = NULL; } while (0)

/* ── Safe array growth helper ─────────────────────────────────────── */
/* Doubles `*cap` until it exceeds `needed`, then reallocs `*buf`.    */
#define OCL_GROW_ARRAY(buf, cap, needed, type) do {                   \
    if ((needed) >= *(cap)) {                                         \
        size_t _nc = *(cap) ? *(cap) * 2 : 8;                        \
        while (_nc <= (needed)) _nc *= 2;                             \
        *(buf) = ocl_realloc(*(buf), _nc * sizeof(type));             \
        *(cap) = _nc;                                                 \
    }                                                                 \
} while (0)

#endif /* OCL_COMMON_H */
