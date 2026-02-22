#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Value constructors */
Value value_int(int64_t i) {
    return (Value){.type = VALUE_INT, .data.int_val = i};
}

Value value_float(double f) {
    return (Value){.type = VALUE_FLOAT, .data.float_val = f};
}

Value value_string(char *s) {
    return (Value){.type = VALUE_STRING, .data.string_val = s};
}

Value value_bool(bool b) {
    return (Value){.type = VALUE_BOOL, .data.bool_val = b};
}

Value value_char(char c) {
    return (Value){.type = VALUE_CHAR, .data.char_val = c};
}

Value value_null(void) {
    return (Value){.type = VALUE_NULL, .data.int_val = 0};
}

/* Convert value to string for printing */
char *value_to_string(Value v) {
    static char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    
    switch (v.type) {
        case VALUE_INT:
            snprintf(buffer, sizeof(buffer), "%ld", v.data.int_val);
            break;
        case VALUE_FLOAT:
            snprintf(buffer, sizeof(buffer), "%f", v.data.float_val);
            break;
        case VALUE_STRING:
            return v.data.string_val ? v.data.string_val : "";
        case VALUE_BOOL:
            return v.data.bool_val ? "true" : "false";
        case VALUE_CHAR:
            snprintf(buffer, sizeof(buffer), "%c", v.data.char_val);
            break;
        case VALUE_NULL:
            return "null";
        default:
            return "unknown";
    }
    return buffer;
}

void value_free(Value v) {
    if (v.type == VALUE_STRING && v.data.string_val) {
        free(v.data.string_val);
    }
}

/* Memory management wrappers */
void *ocl_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "ERROR: Memory allocation failed (%zu bytes)\n", size);
        exit(1);
    }
    return ptr;
}

void *ocl_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "ERROR: Memory reallocation failed (%zu bytes)\n", size);
        exit(1);
    }
    return new_ptr;
}

void ocl_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}
