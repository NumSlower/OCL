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
    union {
        int64_t  int_val;
        double   float_val;
        char    *string_val;
        bool     bool_val;
        char     char_val;
    } data;
} Value;

Value  value_int(int64_t i);
Value  value_float(double f);
Value  value_string(char *s);
Value  value_string_copy(const char *s);
Value  value_bool(bool b);
Value  value_char(char c);
Value  value_null(void);

bool   value_is_truthy(Value v);
void   value_free(Value v);
char  *value_to_string(Value v);

void  *ocl_malloc(size_t size);
void  *ocl_realloc(void *ptr, size_t size);
void   ocl_free(void *ptr);
char  *ocl_strdup(const char *s);

#endif
