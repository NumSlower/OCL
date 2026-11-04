#ifndef OCL_ERRORS_H
#define OCL_ERRORS_H

#include "common.h"

/* ── Error phase ──────────────────────────────────────────────────── */
typedef enum {
    ERROR_LEXER,
    ERROR_PARSER,
    ERROR_TYPE_CHECKER,
    ERROR_RUNTIME
} ErrorPhase;

/* ── Single error record ──────────────────────────────────────────── */
typedef struct {
    ErrorPhase     phase;
    char          *message;   /* heap-owned; freed by error_collector_free */
    SourceLocation location;
    bool           is_warning;
} OclError;

/* ── Collector ────────────────────────────────────────────────────── */
typedef struct {
    OclError *errors;
    size_t    count;
    size_t    capacity;
    size_t    warning_count;
    size_t    error_count;
} ErrorCollector;

/* Lifecycle */
ErrorCollector *error_collector_create(void);
void            error_collector_free(ErrorCollector *ec);
void            error_collector_reset(ErrorCollector *ec);

/* Adding errors / warnings */
void error_add(ErrorCollector *ec, ErrorPhase phase, SourceLocation loc,
               const char *fmt, ...) OCL_PRINTF_LIKE(4, 5);

void error_add_warning(ErrorCollector *ec, ErrorPhase phase, SourceLocation loc,
                       const char *fmt, ...) OCL_PRINTF_LIKE(4, 5);

/* Queries */
bool   error_has_errors(const ErrorCollector *ec);
size_t error_count(const ErrorCollector *ec);
size_t error_warning_count(const ErrorCollector *ec);

/* Output */
void error_print_all(const ErrorCollector *ec);
void error_print_phase(const ErrorCollector *ec, ErrorPhase phase);

#endif /* OCL_ERRORS_H */
