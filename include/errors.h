#ifndef OCL_ERRORS_H
#define OCL_ERRORS_H
#include "common.h"
typedef enum { ERROR_LEXER, ERROR_PARSER, ERROR_TYPE_CHECKER, ERROR_RUNTIME } ErrorType;
typedef struct {
    ErrorType      type;
    const char    *message;
    SourceLocation location;
    bool           is_warning;
} Error;
typedef struct {
    Error  *errors;
    size_t  error_count;
    size_t  capacity;
} ErrorCollector;
ErrorCollector *error_collector_create(void);
void            error_collector_free(ErrorCollector *collector);
void            error_add(ErrorCollector *collector, ErrorType type, SourceLocation loc, const char *format, ...);
void            error_print_all(ErrorCollector *collector);
int             error_get_count(ErrorCollector *collector);
bool            error_has_errors(ErrorCollector *collector);
#endif
