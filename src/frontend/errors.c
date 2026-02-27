#include "common.h"
#include "errors.h"
#include <stdio.h>
#include <string.h>

ErrorCollector *error_collector_create(void) {
    ErrorCollector *collector = ocl_malloc(sizeof(ErrorCollector));
    collector->errors = NULL;
    collector->error_count = 0;
    collector->capacity = 0;
    return collector;
}

void error_collector_free(ErrorCollector *collector) {
    if (!collector) return;
    for (size_t i = 0; i < collector->error_count; i++) {
        if (collector->errors[i].message)
            free((void *)collector->errors[i].message);
    }
    ocl_free(collector->errors);
    ocl_free(collector);
}

void error_add(ErrorCollector *collector, ErrorType type, SourceLocation loc, const char *format, ...) {
    if (!collector) return;
    if (collector->error_count >= collector->capacity) {
        collector->capacity = (collector->capacity == 0) ? 10 : collector->capacity * 2;
        collector->errors = ocl_realloc(collector->errors, collector->capacity * sizeof(Error));
    }
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Error *err = &collector->errors[collector->error_count];
    err->type = type;
    err->message = (const char *)ocl_malloc(strlen(buffer) + 1);
    strcpy((char *)err->message, buffer);
    err->location = loc;
    err->is_warning = false;
    collector->error_count++;
}

void error_print_all(ErrorCollector *collector) {
    if (!collector || collector->error_count == 0) return;
    for (size_t i = 0; i < collector->error_count; i++) {
        Error *err = &collector->errors[i];
        const char *type_str;
        switch (err->type) {
            case ERROR_LEXER:        type_str = "LEXER ERROR";   break;
            case ERROR_PARSER:       type_str = "PARSE ERROR";   break;
            case ERROR_TYPE_CHECKER: type_str = "TYPE ERROR";    break;
            case ERROR_RUNTIME:      type_str = "RUNTIME ERROR"; break;
            default:                 type_str = "ERROR";         break;
        }
        fprintf(stderr, "%s: %s", type_str, err->message);
        if (err->location.filename)
            fprintf(stderr, " [%s:%d:%d]", err->location.filename, err->location.line, err->location.column);
        fprintf(stderr, "\n");
    }
}

int error_get_count(ErrorCollector *collector) { return collector ? (int)collector->error_count : 0; }
bool error_has_errors(ErrorCollector *collector) { return collector && collector->error_count > 0; }
