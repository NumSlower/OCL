#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ── String constructors ──────────────────────────────────────────── */

Value value_string_copy(const char *s) {
    char *dup = ocl_strdup(s ? s : "");
    return (Value){VALUE_STRING, true, {.string_val = dup}};
}

Value value_own_copy(Value v) {
    if (v.type == VALUE_STRING && !v.owned) {
        /* Borrow → owned copy */
        const char *src = v.data.string_val ? v.data.string_val : "";
        return value_string_copy(src);
    }
    return v;   /* already owned (or non-string) — return as-is */
}

/* ── Truthiness ───────────────────────────────────────────────────── */

bool value_is_truthy(Value v) {
    switch (v.type) {
        case VALUE_BOOL:   return v.data.bool_val;
        case VALUE_INT:    return v.data.int_val != 0;
        case VALUE_FLOAT:  return v.data.float_val != 0.0;
        case VALUE_STRING: return v.data.string_val && v.data.string_val[0] != '\0';
        case VALUE_CHAR:   return v.data.char_val != '\0';
        case VALUE_NULL:   return false;
        default:           return false;
    }
}

/* ── String representation ────────────────────────────────────────── */

char *value_to_string(Value v) {
    static char buf[256];
    switch (v.type) {
        case VALUE_INT:    snprintf(buf, sizeof(buf), "%ld", (long)v.data.int_val);  return buf;
        case VALUE_FLOAT:  snprintf(buf, sizeof(buf), "%g",  v.data.float_val);      return buf;
        case VALUE_STRING: return v.data.string_val ? v.data.string_val : "";
        case VALUE_BOOL:   return v.data.bool_val ? "true" : "false";
        case VALUE_CHAR:   buf[0] = v.data.char_val; buf[1] = '\0';                  return buf;
        case VALUE_NULL:   return "null";
        default:           return "?";
    }
}

/* ── Free ─────────────────────────────────────────────────────────── */

void value_free(Value v) {
    /*
     * Only free the string allocation if this Value owns it.
     * Borrowed strings (owned == false) point into memory managed
     * elsewhere (constant pool, another local/global slot) and must
     * not be freed here.
     */
    if (v.type == VALUE_STRING && v.owned && v.data.string_val)
        free(v.data.string_val);
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