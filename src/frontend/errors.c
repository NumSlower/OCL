#include "errors.h"
#include <stdio.h>
#include <string.h>

/* ── Lifecycle ────────────────────────────────────────────────────── */

ErrorCollector *error_collector_create(void) {
    ErrorCollector *ec = ocl_malloc(sizeof(ErrorCollector));
    *ec = (ErrorCollector){0};
    return ec;
}

void error_collector_free(ErrorCollector *ec) {
    if (!ec) return;
    for (size_t i = 0; i < ec->count; i++)
        OCL_FREE(ec->errors[i].message);
    OCL_FREE(ec->errors);
    ocl_free(ec);
}

void error_collector_reset(ErrorCollector *ec) {
    if (!ec) return;
    for (size_t i = 0; i < ec->count; i++)
        OCL_FREE(ec->errors[i].message);
    ec->count = ec->error_count = ec->warning_count = 0;
}

/* ── Internal add ─────────────────────────────────────────────────── */

static void add_record(ErrorCollector *ec, ErrorPhase phase,
                        SourceLocation loc, bool is_warning,
                        const char *fmt, va_list ap)
{
    if (!ec) return;

    OCL_GROW_ARRAY(&ec->errors, &ec->capacity, ec->count, OclError);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);

    OclError *err = &ec->errors[ec->count++];
    err->phase      = phase;
    err->message    = ocl_strdup(buf);
    err->location   = loc;
    err->is_warning = is_warning;

    if (is_warning) ec->warning_count++;
    else            ec->error_count++;
}

/* ── Public add helpers ───────────────────────────────────────────── */

void error_add(ErrorCollector *ec, ErrorPhase phase,
               SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    add_record(ec, phase, loc, false, fmt, ap);
    va_end(ap);
}

void error_add_warning(ErrorCollector *ec, ErrorPhase phase,
                        SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    add_record(ec, phase, loc, true, fmt, ap);
    va_end(ap);
}

/* ── Queries ──────────────────────────────────────────────────────── */

bool   error_has_errors(const ErrorCollector *ec)    { return ec && ec->error_count > 0; }
size_t error_count(const ErrorCollector *ec)         { return ec ? ec->count : 0; }
size_t error_warning_count(const ErrorCollector *ec) { return ec ? ec->warning_count : 0; }

/* ── Output ───────────────────────────────────────────────────────── */

static const char *phase_label(ErrorPhase p) {
    switch (p) {
        case ERROR_LEXER:        return "LEXER ERROR";
        case ERROR_PARSER:       return "PARSE ERROR";
        case ERROR_TYPE_CHECKER: return "TYPE ERROR";
        case ERROR_RUNTIME:      return "RUNTIME ERROR";
        default:                 return "ERROR";
    }
}

static void print_one(const OclError *err) {
    const char *label = err->is_warning ? "WARNING" : phase_label(err->phase);
    if (err->location.filename && err->location.line > 0)
        fprintf(stderr, "%s [%s:%d:%d]: %s\n",
                label,
                err->location.filename,
                err->location.line,
                err->location.column,
                err->message);
    else
        fprintf(stderr, "%s: %s\n", label, err->message);
}

void error_print_all(const ErrorCollector *ec) {
    if (!ec) return;
    for (size_t i = 0; i < ec->count; i++)
        print_one(&ec->errors[i]);
}

void error_print_phase(const ErrorCollector *ec, ErrorPhase phase) {
    if (!ec) return;
    for (size_t i = 0; i < ec->count; i++)
        if (ec->errors[i].phase == phase)
            print_one(&ec->errors[i]);
}
