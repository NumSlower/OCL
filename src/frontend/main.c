#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "codegen.h"
#include "bytecode.h"
#include "vm.h"
#include "ocl_stdlib.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    char *buf = ocl_malloc((size_t)size + 1);
    size_t rd = fread(buf, 1, (size_t)size, f); buf[rd] = '\0';
    fclose(f); return buf;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <file.ocl>\n"
        "Options:\n"
        "  --dump-tokens    Print lexer tokens and exit\n"
        "  --dump-bytecode  Print bytecode disassembly and exit\n"
        "  --no-typecheck   Skip type checker\n"
        "  --time           Show execution time\n"
        "  -h, --help       Show this help\n",
        prog);
}

int main(int argc, char *argv[]) {
    const char *filename   = NULL;
    bool dump_tokens       = false;
    bool dump_bytecode     = false;
    bool skip_typecheck    = false;
    bool show_time         = false;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--dump-tokens"))   dump_tokens    = true;
        else if (!strcmp(argv[i], "--dump-bytecode")) dump_bytecode  = true;
        else if (!strcmp(argv[i], "--no-typecheck"))  skip_typecheck = true;
        else if (!strcmp(argv[i], "--time"))          show_time      = true;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { print_usage(argv[0]); return 0; }
        else if (argv[i][0] != '-') filename = argv[i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); print_usage(argv[0]); return 1; }
    }
    if (!filename) { print_usage(argv[0]); return 1; }

    char *source = read_file(filename);
    if (!source) return 1;

    int exit_code = 0;
    ErrorCollector *errors = error_collector_create();

    Lexer *lexer = lexer_create(source, filename);
    size_t token_count = 0;
    Token *tokens = lexer_tokenize_all(lexer, &token_count);

    if (dump_tokens) {
        for (size_t i = 0; i < token_count; i++)
            printf("[%4zu] line %-4d type %-20d  '%s'\n", i, tokens[i].location.line, tokens[i].type, tokens[i].lexeme ? tokens[i].lexeme : "");
        goto cleanup_tokens;
    }

    {
        bool lex_err = false;
        for (size_t i = 0; i < token_count; i++) {
            if (tokens[i].type == TOKEN_ERROR) {
                fprintf(stderr, "Lex error at %s:%d:%d: %s\n",
                        tokens[i].location.filename ? tokens[i].location.filename : filename,
                        tokens[i].location.line, tokens[i].location.column,
                        tokens[i].lexeme ? tokens[i].lexeme : "unknown");
                lex_err = true;
            }
        }
        if (lex_err) { exit_code = 1; goto cleanup_tokens; }
    }

    {
        Parser *parser = parser_create(tokens, token_count, filename, errors);
        ProgramNode *program = parser_parse(parser);
        parser_free(parser);

        if (error_has_errors(errors)) {
            fprintf(stderr, "Parse errors:\n"); error_print_all(errors);
            exit_code = 1;
            if (program) ast_free((ASTNode *)program);
            goto cleanup_tokens;
        }

        /*
         * Type checking — when errors are found we print them and stop.
         * Codegen and execution are ONLY reached when the program is
         * type-error-free (or --no-typecheck was passed explicitly).
         */
        if (!skip_typecheck && program) {
            TypeChecker *tc = type_checker_create(errors);
            type_checker_check(tc, program);
            int tc_errors = type_checker_get_error_count(tc);
            type_checker_free(tc);

            if (tc_errors > 0 || error_has_errors(errors)) {
                fprintf(stderr, "Type errors — compilation aborted:\n");
                error_print_all(errors);
                exit_code = 1;
                ast_free((ASTNode *)program);
                goto cleanup_tokens;
            }
        }

        Bytecode *bytecode = bytecode_create();
        CodeGenerator *gen = codegen_create(errors);
        bool ok = codegen_generate(gen, program, bytecode);
        codegen_free(gen);

        if (!ok || error_has_errors(errors)) {
            fprintf(stderr, "Codegen errors:\n"); error_print_all(errors);
            exit_code = 1; bytecode_free(bytecode);
            if (program) ast_free((ASTNode *)program);
            goto cleanup_tokens;
        }

        if (dump_bytecode) {
            bytecode_dump(bytecode);
            bytecode_free(bytecode);
            if (program) ast_free((ASTNode *)program);
            goto cleanup_tokens;
        }

        stdlib_init();
        VM *vm = vm_create(bytecode);
        struct timespec t0, t1;
        if (show_time) clock_gettime(CLOCK_MONOTONIC, &t0);
        exit_code = vm_execute(vm);
        if (show_time) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = (t1.tv_sec - t0.tv_sec)*1e6 + (t1.tv_nsec - t0.tv_nsec)/1e3;
            if (us < 1000) fprintf(stderr, "[time: %.1f µs]\n", us);
            else if (us < 1e6) fprintf(stderr, "[time: %.2f ms]\n", us/1000);
            else fprintf(stderr, "[time: %.3f s]\n", us/1e6);
        }
        vm_free(vm);
        stdlib_cleanup();
        bytecode_free(bytecode);
        if (program) ast_free((ASTNode *)program);
    }

cleanup_tokens:
    tokens_free(tokens, token_count);
    lexer_free(lexer);
    error_collector_free(errors);
    ocl_free(source);
    return exit_code;
}