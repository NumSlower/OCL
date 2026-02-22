#ifndef OCL_COMMON_H
#define OCL_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Source location tracking */
typedef struct {
    int line;
    int column;
    const char *filename;
} SourceLocation;

/* Value types for the runtime */
typedef enum {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_CHAR,
    VALUE_NULL
} ValueType;

/* Union to hold different value types */
typedef struct {
    ValueType type;
    union {
        int64_t int_val;
        double float_val;
        char *string_val;
        bool bool_val;
        char char_val;
    } data;
} Value;

/* Utility functions */
Value value_int(int64_t i);
Value value_float(double f);
Value value_string(char *s);
Value value_bool(bool b);
Value value_char(char c);
Value value_null(void);

void value_free(Value v);
char *value_to_string(Value v);

/* Memory utilities */
void *ocl_malloc(size_t size);
void *ocl_realloc(void *ptr, size_t size);
void ocl_free(void *ptr);

#endif /* OCL_COMMON_H */
