/*
 * main.c — OCL interpreter entry point
 *
 * Pipeline:
 *   source text → Lexer → tokens → Parser → AST → TypeChecker → AST
 *              → CodeGenerator → Bytecode → VM → result
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "codegen.h"
#include "bytecode.h"
#include "vm.h"
#include "ocl_stdlib.h"

/* ── Helpers ──────────────────────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = ocl_malloc((size_t)size + 1);
    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <file.ocl>\n"
        "Options:\n"
        "  --dump-tokens    Print lexer tokens and exit\n"
        "  --dump-ast       Print AST and exit\n"
        "  --dump-bytecode  Print bytecode disassembly and exit\n"
        "  --no-typecheck   Skip type checker\n"
        "  -h, --help       Show this help\n",
        prog);
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *filename    = NULL;
    bool dump_tokens        = false;
    bool dump_ast           = false;
    bool dump_bytecode      = false;
    bool skip_typecheck     = false;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--dump-tokens"))    dump_tokens    = true;
        else if (!strcmp(argv[i], "--dump-ast"))       dump_ast       = true;
        else if (!strcmp(argv[i], "--dump-bytecode"))  dump_bytecode  = true;
        else if (!strcmp(argv[i], "--no-typecheck"))   skip_typecheck = true;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]); return 0;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!filename) {
        print_usage(argv[0]);
        return 1;
    }

    /* 1. Read source */
    char *source = read_file(filename);
    if (!source) return 1;

    int exit_code = 0;
    ErrorCollector *errors = error_collector_create();

    /* 2. Lex */
    Lexer *lexer = lexer_create(source, filename);
    size_t token_count = 0;
    Token *tokens = lexer_tokenize_all(lexer, &token_count);

    if (dump_tokens) {
        for (size_t i = 0; i < token_count; i++) {
            printf("[%4zu] line %-4d type %-20d  '%s'\n",
                   i, tokens[i].location.line, tokens[i].type,
                   tokens[i].lexeme ? tokens[i].lexeme : "");
        }
        goto cleanup_tokens;
    }

    /* Check for lex errors — lexer produces TOKEN_ERROR entries */
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

    /* 3. Parse */
    Parser *parser = parser_create(tokens, token_count, filename, errors);
    ProgramNode *program = parser_parse(parser);
    parser_free(parser);

    if (dump_ast && program) {
        /* ast_print(program); */   /* uncomment if you implement ast_print */
        fprintf(stderr, "(--dump-ast: ast_print not implemented)\n");
        goto cleanup_ast;
    }

    if (error_has_errors(errors)) {
        fprintf(stderr, "Parse errors:\n");
        error_print_all(errors);
        exit_code = 1;
        goto cleanup_ast;
    }

    /* 4. Type check (optional) */
    if (!skip_typecheck && program) {
        TypeChecker *tc = type_checker_create(errors);
        type_checker_check(tc, program);
        type_checker_free(tc);

        /* Print warnings even if no errors */
        if (error_get_count(errors) > 0 || error_has_errors(errors)) {
            error_print_all(errors);
        }

        if (error_has_errors(errors)) {
            exit_code = 1;
            goto cleanup_ast;
        }
    }

    /* 5. Code generation */
    Bytecode *bytecode = bytecode_create();
    CodeGenerator *gen = codegen_create(errors);
    bool ok = codegen_generate(gen, program, bytecode);
    codegen_free(gen);

    if (!ok || error_has_errors(errors)) {
        fprintf(stderr, "Codegen errors:\n");
        error_print_all(errors);
        exit_code = 1;
        bytecode_free(bytecode);
        goto cleanup_ast;
    }

    if (dump_bytecode) {
        bytecode_dump(bytecode);
        bytecode_free(bytecode);
        goto cleanup_ast;
    }

    /* 6. Execute */
    stdlib_init();
    VM *vm = vm_create(bytecode);
    exit_code = vm_execute(vm);
    vm_free(vm);
    stdlib_cleanup();
    bytecode_free(bytecode);

cleanup_ast:
    if (program) ast_free((ASTNode *)program);

cleanup_tokens:
    tokens_free(tokens, token_count);
    lexer_free(lexer);

    error_collector_free(errors);
    ocl_free(source);

    return exit_code;
}