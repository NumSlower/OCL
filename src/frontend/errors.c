#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
#  define strcasecmp _stricmp
#else
#  include <strings.h>
#endif

#if defined(_WIN32)
#  include <io.h>
#  define OCL_ISATTY _isatty
#  define OCL_FILENO _fileno
#else
#  include <unistd.h>
#  define OCL_ISATTY isatty
#  define OCL_FILENO fileno
#endif

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fputs("ocl: fatal: out of memory\n", stderr); abort(); }
    return p;
}

static void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n);
    if (!p) { fputs("ocl: fatal: out of memory\n", stderr); abort(); }
    return p;
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = xmalloc(len + 1);
    memcpy(d, s, len + 1);
    return d;
}

static bool env_is_false(const char *s) {
    if (!s) return false;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return false;
    return (!strcmp(s, "0") || !strcasecmp(s, "false") || !strcasecmp(s, "no") || !strcasecmp(s, "off"));
}

static bool colors_enabled(void) {
    static int cached = -1;
    if (cached != -1) return cached == 1;

    const char *force = getenv("OCL_COLOR");
    if (force) {
        cached = env_is_false(force) ? 0 : 1;
        return cached == 1;
    }
    if (getenv("NO_COLOR")) {
        cached = 0;
        return false;
    }

    cached = OCL_ISATTY(OCL_FILENO(stderr)) ? 1 : 0;
    return cached == 1;
}

/* ── Lifecycle ────────────────────────────────────────────────────── */

ErrorCollector *error_collector_create(void) {
    /* Use plain malloc/free so optional memtrace doesn't report diagnostics memory as leaks. */
    ErrorCollector *ec = xmalloc(sizeof(ErrorCollector));
    memset(ec, 0, sizeof(*ec));
    return ec;
}

void error_collector_free(ErrorCollector *ec) {
    if (!ec) return;
    for (size_t i = 0; i < ec->count; i++)
        free(ec->errors[i].message);
    free(ec->errors);
    free(ec);
}

void error_collector_reset(ErrorCollector *ec) {
    if (!ec) return;
    for (size_t i = 0; i < ec->count; i++)
        free(ec->errors[i].message);
    ec->count = ec->error_count = ec->warning_count = 0;
}

/* ── Internal add ─────────────────────────────────────────────────── */

static void ensure_capacity(ErrorCollector *ec, size_t needed) {
    if (needed <= ec->capacity) return;
    size_t new_cap = ec->capacity ? ec->capacity * 2 : 8;
    while (new_cap < needed) new_cap *= 2;
    ec->errors = xrealloc(ec->errors, new_cap * sizeof(OclError));
    ec->capacity = new_cap;
}

static void add_record(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase,
                         SourceLocation loc, bool is_warning,
                         const char *fmt, va_list ap)
{
    if (!ec) return;

    ensure_capacity(ec, ec->count + 1);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);

    OclError *err = &ec->errors[ec->count++];
    err->kind       = kind;
    err->phase      = phase;
    err->message    = xstrdup(buf);
    err->location   = loc;
    err->related_location = LOC_NONE;
    err->is_warning = is_warning;

    if (is_warning) ec->warning_count++;
    else            ec->error_count++;
}

/* ── Public add helpers ───────────────────────────────────────────── */

void error_add(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase,
               SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    add_record(ec, kind, phase, loc, false, fmt, ap);
    va_end(ap);
}

void error_add_with_related(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase,
                            SourceLocation loc, SourceLocation related_loc,
                            const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    add_record(ec, kind, phase, loc, false, fmt, ap);
    va_end(ap);
    if (ec && ec->count > 0)
        ec->errors[ec->count - 1].related_location = related_loc;
}

void error_add_warning(ErrorCollector *ec, ErrorKind kind, ErrorPhase phase,
                         SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    add_record(ec, kind, phase, loc, true, fmt, ap);
    va_end(ap);
}

/* ── Queries ──────────────────────────────────────────────────────── */

bool   error_has_errors(const ErrorCollector *ec)    { return ec && ec->error_count > 0; }
size_t error_count(const ErrorCollector *ec)         { return ec ? ec->count : 0; }
size_t error_warning_count(const ErrorCollector *ec) { return ec ? ec->warning_count : 0; }

/* ── Output ───────────────────────────────────────────────────────── */

static const char *kind_label(ErrorKind k, bool is_warning) {
    if (is_warning) {
        switch (k) {
            case ERRK_SYNTAX:    return "SYNTAX WARNING";
            case ERRK_TYPE:      return "TYPE WARNING";
            case ERRK_OPERATION: return "OPERATION WARNING";
            case ERRK_MEMORY:    return "MEMORY WARNING";
            case ERRK_LOGIC:     return "LOGIC WARNING";
        }
    }
    switch (k) {
        case ERRK_SYNTAX:    return "SYNTAX ERROR";
        case ERRK_TYPE:      return "TYPE ERROR";
        case ERRK_OPERATION: return "OPERATION ERROR";
        case ERRK_MEMORY:    return "MEMORY LEAK";
        case ERRK_LOGIC:     return "LOGIC ERROR";
    }
    return is_warning ? "WARNING" : "ERROR";
}

static const char *kind_color(ErrorKind k) {
    switch (k) {
        case ERRK_SYNTAX:    return "\x1b[31m";   /* red */
        case ERRK_TYPE:      return "\x1b[35m";   /* magenta */
        case ERRK_OPERATION: return "\x1b[33m";   /* yellow */
        case ERRK_MEMORY:    return "\x1b[1;31m"; /* bold red */
        case ERRK_LOGIC:     return "\x1b[36m";   /* cyan */
    }
    return "\x1b[0m";
}

#define OCL_ERROR_CONTEXT_LINES 4

static char *read_source_line(FILE *f) {
    size_t cap = 128;
    size_t len = 0;
    char *line = NULL;

    if (!f) return NULL;

    for (;;) {
        int ch = fgetc(f);
        if (ch == EOF) {
            if (!line)
                return NULL;
            break;
        }

        if (!line)
            line = xmalloc(cap);

        if (ch == '\r') {
            int next = fgetc(f);
            if (next != '\n' && next != EOF)
                ungetc(next, f);
            break;
        }
        if (ch == '\n')
            break;

        if (len + 1 >= cap) {
            cap *= 2;
            line = xrealloc(line, cap);
        }
        line[len++] = (char)ch;
    }

    if (!line)
        line = xstrdup("");

    line[len] = '\0';
    return line;
}

static char *expand_tabs_for_display(const char *line, int column, size_t *caret_offset) {
    size_t src_len = strlen(line ? line : "");
    size_t cap = src_len * 4 + 1;
    size_t len = 0;
    size_t caret = (size_t)-1;
    int raw_col = 1;
    char *out = xmalloc(cap > 0 ? cap : 1);

    if (!line) line = "";
    if (column < 1) column = 1;

    for (const unsigned char *p = (const unsigned char *)line; *p; ++p, raw_col++) {
        if (raw_col == column && caret == (size_t)-1)
            caret = len;

        if (*p == '\t') {
            size_t spaces = 4 - (len % 4);
            while (spaces-- > 0)
                out[len++] = ' ';
            continue;
        }

        out[len++] = (char)*p;
    }

    if (caret == (size_t)-1)
        caret = len;

    out[len] = '\0';
    if (caret_offset)
        *caret_offset = caret;
    return out;
}

static int line_number_width(int line_number) {
    int width = 1;
    while (line_number >= 10) {
        line_number /= 10;
        width++;
    }
    return width;
}

static void print_excerpt_line(int width, int line_number, const char *text,
                               bool is_focus, bool is_related,
                               const char *focus_color, const char *related_color,
                               const char *reset) {
    if (is_focus) {
        if (focus_color && reset)
            fprintf(stderr, "%s>%s %*d | %s%s%s\n",
                    focus_color, reset, width, line_number, focus_color, text, reset);
        else
            fprintf(stderr, "> %*d | %s\n", width, line_number, text);
        return;
    }

    if (is_related) {
        if (related_color && reset)
            fprintf(stderr, "  %s%*d | %s%s%s\n",
                    related_color, width, line_number, related_color, text, reset);
        else
            fprintf(stderr, "  %*d | %s\n", width, line_number, text);
        return;
    }

    fprintf(stderr, "  %*d | %s\n", width, line_number, text);
}

static bool print_source_excerpt(const OclError *err, bool use_color, const char *color, const char *reset) {
    FILE *f;
    char *before[OCL_ERROR_CONTEXT_LINES] = {0};
    int before_lines[OCL_ERROR_CONTEXT_LINES] = {0};
    size_t before_count = 0;
    int current_line = 0;
    bool printed = false;
    int related_line = 0;

    if (!err || !err->location.filename || err->location.line <= 0)
        return false;

    if (err->related_location.filename &&
        err->location.filename &&
        !strcmp(err->related_location.filename, err->location.filename) &&
        err->related_location.line > 0) {
        related_line = err->related_location.line;
    }

    f = fopen(err->location.filename, "rb");
    if (!f)
        return false;

    for (;;) {
        char *line = read_source_line(f);
        if (!line)
            break;

        current_line++;
        if (current_line < err->location.line) {
            if (current_line >= err->location.line - OCL_ERROR_CONTEXT_LINES) {
                if (before_count == OCL_ERROR_CONTEXT_LINES) {
                    free(before[0]);
                    for (size_t i = 1; i < before_count; i++) {
                        before[i - 1] = before[i];
                        before_lines[i - 1] = before_lines[i];
                    }
                    before_count--;
                }
                before[before_count] = line;
                before_lines[before_count] = current_line;
                before_count++;
            } else {
                free(line);
            }
            continue;
        }

        if (current_line == err->location.line) {
            int width = line_number_width(err->location.line + OCL_ERROR_CONTEXT_LINES);

            fputc('\n', stderr);
            for (size_t i = 0; i < before_count; i++) {
                char *display = expand_tabs_for_display(before[i], 0, NULL);
                print_excerpt_line(width, before_lines[i], display, false,
                                   before_lines[i] == related_line,
                                   NULL,
                                   use_color ? "\x1b[33m" : NULL,
                                   use_color ? reset : NULL);
                free(display);
            }

            size_t caret_offset = 0;
            char *focus = expand_tabs_for_display(line, err->location.column, &caret_offset);
            print_excerpt_line(width, current_line, focus, true, false,
                               use_color ? color : NULL,
                               NULL,
                               use_color ? reset : NULL);
            fprintf(stderr, "  %*s | %*s%s^%s\n",
                    width, "",
                    (int)caret_offset, "",
                    use_color ? color : "",
                    use_color ? reset : "");
            free(focus);
            free(line);

            for (int i = 0; i < OCL_ERROR_CONTEXT_LINES; i++) {
                char *after = read_source_line(f);
                if (!after)
                    break;
                current_line++;
                char *display = expand_tabs_for_display(after, 0, NULL);
                print_excerpt_line(width, current_line, display, false,
                                   current_line == related_line,
                                   NULL,
                                   use_color ? "\x1b[33m" : NULL,
                                   use_color ? reset : NULL);
                free(display);
                free(after);
            }

            printed = true;
            break;
        }

        free(line);
    }

    for (size_t i = 0; i < before_count; i++)
        free(before[i]);
    fclose(f);
    return printed;
}

static void print_one(const OclError *err) {
    bool use_color = colors_enabled();
    const char *label = kind_label(err->kind, err->is_warning);
    const char *c = use_color ? kind_color(err->kind) : "";
    const char *r = use_color ? "\x1b[0m" : "";
    if (err->location.filename && err->location.line > 0)
        fprintf(stderr, "%s%s%s [%s:%d:%d]: %s\n",
                c, label, r,
                err->location.filename,
                err->location.line,
                err->location.column,
                err->message);
    else
        fprintf(stderr, "%s%s%s: %s\n", c, label, r, err->message);

    if (print_source_excerpt(err, use_color, c, r))
        fputc('\n', stderr);
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
