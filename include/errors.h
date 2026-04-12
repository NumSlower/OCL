#ifndef OCL_ERRORS_H
#define OCL_ERRORS_H

#include "common.h"

/* ── Error phase ──────────────────────────────────────────────────── */
typedef enum {
    ERROR_LEXER,
    ERROR_PARSER,
    ERROR_TYPE_CHECKER,
    ERROR_CODEGEN,
    ERROR_RUNTIME
} ErrorPhase;

/* ── User-facing error kind (category) ────────────────────────────── */
typedef enum {
    ERRK_SYNTAX,
    ERRK_TYPE,
    ERRK_OPERATION,
    ERRK_MEMORY,
    ERRK_LOGIC
} ErrorKind;

/* ── Single error record ──────────────────────────────────────────── */
typedef struct {
    ErrorKind      kind;
    ErrorPhase     phase;
    char          *message;   /* heap-owned; freed by error_collector_free */
    SourceLocation location;
    SourceLocation related_location;
    bool           is_warning;
} OclError;

/* ── Collector ────────────────────────────────────────────────────── */
typedef struct ErrorCollector {
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
void error_add(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase, SourceLocation loc,
               const char *fmt, ...) OCL_PRINTF_LIKE(5, 6);

void error_add_with_related(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase,
                            SourceLocation loc, SourceLocation related_loc,
                            const char *fmt, ...) OCL_PRINTF_LIKE(6, 7);

void error_add_warning(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase, SourceLocation loc,
                       const char *fmt, ...) OCL_PRINTF_LIKE(5, 6);

/* Queries */
bool   error_has_errors(const ErrorCollector *ec);
size_t error_count(const ErrorCollector *ec);
size_t error_warning_count(const ErrorCollector *ec);

/* Output */
void error_print_all(const ErrorCollector *ec);
void error_print_phase(const ErrorCollector *ec, ErrorPhase phase);

#endif /* OCL_ERRORS_H */
