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
    VALUE_ARRAY,
    VALUE_NULL
} ValueType;

/* Forward declaration for array boxing */
typedef struct OclArray OclArray;

typedef struct {
    ValueType type;
    bool owned;
    union {
        int64_t  int_val;
        double   float_val;
        char    *string_val;
        bool     bool_val;
        char     char_val;
        OclArray *array_val;   /* VALUE_ARRAY */
    } data;
} Value;

/* ── Array heap object ───────────────────────────────────────────── */
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

/* ── Constructors ─────────────────────────────────────────────────── */

static inline Value value_int(int64_t i)  { return (Value){VALUE_INT,   false, {.int_val   = i}}; }
static inline Value value_float(double f) { return (Value){VALUE_FLOAT, false, {.float_val = f}}; }
static inline Value value_bool(bool b)    { return (Value){VALUE_BOOL,  false, {.bool_val  = b}}; }
static inline Value value_char(char c)    { return (Value){VALUE_CHAR,  false, {.char_val  = c}}; }
static inline Value value_null(void)      { return (Value){VALUE_NULL,  false, {.int_val   = 0}}; }

static inline Value value_array(OclArray *a) {
    ocl_array_retain(a);
    return (Value){VALUE_ARRAY, true, {.array_val = a}};
}

static inline Value value_string(char *s) {
    return (Value){VALUE_STRING, true, {.string_val = s}};
}

Value  value_string_copy(const char *s);

static inline Value value_string_borrow(const char *s) {
    return (Value){VALUE_STRING, false, {.string_val = (char *)s}};
}

Value value_own_copy(Value v);

/* ── Utilities ────────────────────────────────────────────────────── */

bool   value_is_truthy(Value v);
void   value_free(Value v);
const char *value_type_name(ValueType t);

/*
 * value_to_string — returns a pointer into a rotating pool of
 * OCL_VTOS_POOL_COUNT static buffers (each OCL_VTOS_BUF_SIZE bytes).
 *
 * Callers MUST NOT free the result.
 * Callers needing the string beyond the next few calls should strdup() it.
 * Up to OCL_VTOS_POOL_COUNT nested calls (e.g. arrays-of-arrays) are safe.
 */
#define OCL_VTOS_POOL_COUNT 8
#define OCL_VTOS_BUF_SIZE   8192
char  *value_to_string(Value v);

/* ── Memory helpers ───────────────────────────────────────────────── */
void  *ocl_malloc(size_t size);
void  *ocl_realloc(void *ptr, size_t size);
void   ocl_free(void *ptr);
char  *ocl_strdup(const char *s);

#endif