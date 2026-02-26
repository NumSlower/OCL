#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "errors.h"
#include "bytecode.h"
#include "codegen.h"
#include "vm.h"
#include "common.h"

static void print_usage(const char *program_name) {
    printf("Usage: %s [options] <source_file.ocl>\n", program_name);
    printf("Options:\n");
    printf("  --time    Print execution time after the program finishes\n");
}

/* Return monotonic time in seconds with nanosecond precision */
static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char *argv[]) {
    const char *filename  = NULL;
    bool        show_time = false;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--time") == 0) {
            show_time = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (filename) {
                fprintf(stderr, "ERROR: Multiple source files specified\n");
                print_usage(argv[0]);
                return 1;
            }
            filename = argv[i];
        }
    }

    if (!filename) {
        print_usage(argv[0]);
        return 1;
    }

    /* ── Read source file ──────────────────────────────────────── */
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "ERROR: Could not open file '%s'\n", filename);
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char  *source     = ocl_malloc((size_t)file_size + 1);
    size_t bytes_read = fread(source, 1, (size_t)file_size, file);
    source[bytes_read] = '\0';
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "ERROR: Failed to read entire file\n");
        ocl_free(source);
        return 1;
    }

    ErrorCollector *errors = error_collector_create();

    /* ── Lexical analysis ──────────────────────────────────────── */
    Lexer  *lexer       = lexer_create(source, filename);
    size_t  token_count = 0;
    Token  *tokens      = lexer_tokenize_all(lexer, &token_count);
    lexer_free(lexer);

    if (error_has_errors(errors)) {
        error_print_all(errors);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 1;
    }

    /* ── Parsing ───────────────────────────────────────────────── */
    Parser      *parser  = parser_create(tokens, token_count, filename, errors);
    ProgramNode *program = parser_parse(parser);
    parser_free(parser);

    if (error_has_errors(errors)) {
        error_print_all(errors);
        tokens_free(tokens, token_count);
        ocl_free(source);
        ast_free((ASTNode *)program);
        error_collector_free(errors);
        return 1;
    }

    tokens_free(tokens, token_count);
    tokens = NULL;

    /* ── Type checking ─────────────────────────────────────────── */
    TypeChecker *type_checker = type_checker_create(errors);
    bool type_check_ok = type_checker_check(type_checker, program);
    type_checker_free(type_checker);

    if (!type_check_ok || error_has_errors(errors)) {
        error_print_all(errors);
        ocl_free(source);
        ast_free((ASTNode *)program);
        error_collector_free(errors);
        return 1;
    }

    /* ── Code generation ───────────────────────────────────────── */
    Bytecode      *bytecode = bytecode_create();
    CodeGenerator *codegen  = codegen_create(errors);
    bool codegen_ok = codegen_generate(codegen, program, bytecode);
    codegen_free(codegen);

    ast_free((ASTNode *)program);
    program = NULL;

    if (!codegen_ok || error_has_errors(errors)) {
        error_print_all(errors);
        ocl_free(source);
        bytecode_free(bytecode);
        error_collector_free(errors);
        return 1;
    }

    /* ── Execution ─────────────────────────────────────────────── */
    double  t_start   = show_time ? now_seconds() : 0.0;
    VM     *vm        = vm_create(bytecode);
    int     exit_code = vm_execute(vm);
    double  t_end     = show_time ? now_seconds() : 0.0;
    vm_free(vm);
    bytecode_free(bytecode);

    /* ── Timing report ─────────────────────────────────────────── */
    if (show_time) {
        double elapsed = t_end - t_start;
        if (elapsed < 1e-3)
            fprintf(stderr, "\n[time] %.3f µs\n", elapsed * 1e6);
        else if (elapsed < 1.0)
            fprintf(stderr, "\n[time] %.3f ms\n", elapsed * 1e3);
        else
            fprintf(stderr, "\n[time] %.6f s\n", elapsed);
    }

    /* ── Cleanup ───────────────────────────────────────────────── */
    ocl_free(source);
    error_collector_free(errors);

    return exit_code;
}