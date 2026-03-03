#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ── Array heap object ────────────────────────────────────────────── */

OclArray *ocl_array_new(size_t initial_cap) {
    OclArray *a = ocl_malloc(sizeof(OclArray));
    a->capacity = initial_cap > 0 ? initial_cap : 8;
    a->elements = ocl_malloc(a->capacity * sizeof(Value));
    for (size_t i = 0; i < a->capacity; i++) a->elements[i] = value_null();
    a->length   = 0;
    a->refcount = 1;  /* creator holds one reference; must release when done */
    return a;
}

void ocl_array_retain(OclArray *a) {
    if (a) a->refcount++;
}

void ocl_array_release(OclArray *a) {
    if (!a) return;
    a->refcount--;
    if (a->refcount <= 0) {
        for (size_t i = 0; i < a->length; i++) value_free(a->elements[i]);
        ocl_free(a->elements);
        ocl_free(a);
    }
}

static void array_ensure_cap(OclArray *a, size_t idx) {
    if (idx < a->capacity) return;
    size_t new_cap = a->capacity * 2;
    if (new_cap <= idx) new_cap = idx + 8;
    a->elements = ocl_realloc(a->elements, new_cap * sizeof(Value));
    for (size_t i = a->capacity; i < new_cap; i++) a->elements[i] = value_null();
    a->capacity = new_cap;
}

void ocl_array_set(OclArray *a, size_t idx, Value v) {
    array_ensure_cap(a, idx);
    value_free(a->elements[idx]);
    a->elements[idx] = value_own_copy(v);
    if (idx >= a->length) a->length = idx + 1;
}

Value ocl_array_get(OclArray *a, size_t idx) {
    if (!a || idx >= a->length) return value_null();
    Value v = a->elements[idx];
    if (v.type == VALUE_STRING) return value_string_borrow(v.data.string_val);
    if (v.type == VALUE_ARRAY)  { ocl_array_retain(v.data.array_val); return v; }
    return v;
}

void ocl_array_push(OclArray *a, Value v) {
    ocl_array_set(a, a->length, v);
}

/* ── String constructors ──────────────────────────────────────────── */

Value value_string_copy(const char *s) {
    char *dup = ocl_strdup(s ? s : "");
    return (Value){VALUE_STRING, true, {.string_val = dup}};
}

Value value_own_copy(Value v) {
    if (v.type == VALUE_STRING && !v.owned) {
        const char *src = v.data.string_val ? v.data.string_val : "";
        return value_string_copy(src);
    }
    if (v.type == VALUE_ARRAY && v.data.array_val) {
        ocl_array_retain(v.data.array_val);
        return v;
    }
    return v;
}

/* ── Truthiness ───────────────────────────────────────────────────── */

bool value_is_truthy(Value v) {
    switch (v.type) {
        case VALUE_BOOL:   return v.data.bool_val;
        case VALUE_INT:    return v.data.int_val != 0;
        case VALUE_FLOAT:  return v.data.float_val != 0.0;
        case VALUE_STRING: return v.data.string_val && v.data.string_val[0] != '\0';
        case VALUE_CHAR:   return v.data.char_val != '\0';
        case VALUE_ARRAY:  return v.data.array_val && v.data.array_val->length > 0;
        case VALUE_NULL:   return false;
        default:           return false;
    }
}

/* ── String representation — rotating static buffer pool ─────────────
 *
 * Returns a pointer into one of OCL_VTOS_POOL_COUNT static buffers,
 * cycling round-robin.  Callers must NOT free the result.  Callers that
 * need to keep the string beyond the next few calls should strdup() it.
 *
 * Using a pool (rather than a single static buffer) means nested calls
 * — e.g. printing arrays whose elements are also arrays — each get their
 * own buffer slot and don't corrupt one another, as long as nesting depth
 * stays below OCL_VTOS_POOL_COUNT.
 */
#define OCL_VTOS_POOL_COUNT 8
#define OCL_VTOS_BUF_SIZE   8192

char *value_to_string(Value v) {
    static char pool[OCL_VTOS_POOL_COUNT][OCL_VTOS_BUF_SIZE];
    static int  slot = 0;

    char *out = pool[slot];
    slot = (slot + 1) % OCL_VTOS_POOL_COUNT;

    switch (v.type) {
        case VALUE_INT:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%ld", (long)v.data.int_val);
            break;
        case VALUE_FLOAT:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%g", v.data.float_val);
            break;
        case VALUE_STRING:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%s",
                     v.data.string_val ? v.data.string_val : "");
            break;
        case VALUE_BOOL:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%s",
                     v.data.bool_val ? "true" : "false");
            break;
        case VALUE_CHAR:
            out[0] = v.data.char_val; out[1] = '\0';
            break;
        case VALUE_NULL:
            snprintf(out, OCL_VTOS_BUF_SIZE, "null");
            break;
        case VALUE_ARRAY: {
            if (!v.data.array_val) { out[0]='['; out[1]=']'; out[2]='\0'; break; }
            size_t pos = 0;
            out[pos++] = '[';
            for (size_t i = 0; i < v.data.array_val->length; i++) {
                if (i > 0) {
                    if (pos + 2 < OCL_VTOS_BUF_SIZE - 1) {
                        out[pos++] = ','; out[pos++] = ' ';
                    }
                }
                /* Recursive call uses the NEXT pool slot, not ours */
                const char *elem = value_to_string(v.data.array_val->elements[i]);
                size_t elen = strlen(elem);
                if (pos + elen < OCL_VTOS_BUF_SIZE - 2) {
                    memcpy(out + pos, elem, elen);
                    pos += elen;
                }
            }
            if (pos < OCL_VTOS_BUF_SIZE - 1) out[pos++] = ']';
            out[pos] = '\0';
            break;
        }
        default:
            out[0] = '?'; out[1] = '\0';
            break;
    }
    return out;
}

/* ── Type name ────────────────────────────────────────────────────── */

const char *value_type_name(ValueType t) {
    switch (t) {
        case VALUE_INT:    return "Int";
        case VALUE_FLOAT:  return "Float";
        case VALUE_STRING: return "String";
        case VALUE_BOOL:   return "Bool";
        case VALUE_CHAR:   return "Char";
        case VALUE_ARRAY:  return "Array";
        case VALUE_NULL:   return "Null";
        default:           return "Unknown";
    }
}

/* ── Free ─────────────────────────────────────────────────────────── */

void value_free(Value v) {
    if (v.type == VALUE_STRING && v.owned && v.data.string_val)
        free(v.data.string_val);
    else if (v.type == VALUE_ARRAY && v.owned && v.data.array_val)
        ocl_array_release(v.data.array_val);
}

/* ── Memory helpers ───────────────────────────────────────────────── */

void *ocl_malloc(size_t size) {
    void *p = malloc(size);
    if (!p && size > 0) { fputs("OCL: out of memory\n", stderr); exit(1); }
    return p;
}

void *ocl_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size > 0) { fputs("OCL: out of memory (realloc)\n", stderr); exit(1); }
    return p;
}

void ocl_free(void *ptr) { free(ptr); }

char *ocl_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = ocl_malloc(len + 1);
    memcpy(d, s, len + 1);
    return d;
}