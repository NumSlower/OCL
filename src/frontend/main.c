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

/* ── CLI options ──────────────────────────────────────────────────── */
typedef struct {
    const char *filename;
    bool        dump_tokens;
    bool        dump_bytecode;
    bool        skip_typecheck;
    bool        show_time;
} Options;

/* ── Helpers ──────────────────────────────────────────────────────── */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <file.ocl>\n"
        "\n"
        "Options:\n"
        "  --dump-tokens    Print lexer tokens and exit\n"
        "  --dump-bytecode  Print bytecode disassembly and exit\n"
        "  --no-typecheck   Skip the type checker\n"
        "  --time           Report execution time\n"
        "  -h, --help       Show this help\n",
        prog);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ocl: cannot open '%s': ", path);
        perror(NULL);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "ocl: fseek failed for '%s'\n", path);
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "ocl: ftell failed for '%s'\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    char  *buf = ocl_malloc((size_t)size + 1);
    size_t rd  = fread(buf, 1, (size_t)size, f);
    buf[rd] = '\0';

    if (ferror(f)) {
        fprintf(stderr, "ocl: read error on '%s'\n", path);
        ocl_free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

static bool parse_options(int argc, char *argv[], Options *out) {
    *out = (Options){0};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-tokens"))        out->dump_tokens   = true;
        else if (!strcmp(argv[i], "--dump-bytecode")) out->dump_bytecode = true;
        else if (!strcmp(argv[i], "--no-typecheck"))  out->skip_typecheck = true;
        else if (!strcmp(argv[i], "--time"))          out->show_time     = true;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            exit(0);
        } else if (argv[i][0] != '-') {
            if (out->filename) {
                fprintf(stderr, "ocl: multiple source files specified\n");
                return false;
            }
            out->filename = argv[i];
        } else {
            fprintf(stderr, "ocl: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return false;
        }
    }
    if (!out->filename) {
        print_usage(argv[0]);
        return false;
    }
    return true;
}

static bool check_errors(ErrorCollector *ec, const char *phase_desc) {
    if (!error_has_errors(ec)) return true;
    fprintf(stderr, "%s — aborting:\n", phase_desc);
    error_print_all(ec);
    return false;
}

/* ── Entry point ──────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    Options opts;
    if (!parse_options(argc, argv, &opts))
        return 1;

    char *source = read_file(opts.filename);
    if (!source) return 1;

    int             exit_code = 0;
    ErrorCollector *errors    = error_collector_create();

    /* ── Stage 1: Lex ───────────────────────────────────────────── */
    Lexer  *lexer       = lexer_create(source, opts.filename);
    size_t  token_count = 0;
    Token  *tokens      = lexer_tokenize_all(lexer, &token_count);
    lexer_free(lexer);

    if (opts.dump_tokens) {
        for (size_t i = 0; i < token_count; i++) {
            printf("[%4zu] line %-4d type %-20d  '%s'\n",
                   i,
                   tokens[i].location.line,
                   tokens[i].type,
                   tokens[i].lexeme ? tokens[i].lexeme : "");
        }
        goto cleanup;
    }

    {
        bool lex_err = false;
        for (size_t i = 0; i < token_count; i++) {
            if (tokens[i].type == TOKEN_ERROR) {
                fprintf(stderr, "LEX ERROR [%s:%d:%d]: unexpected character '%s'\n",
                        tokens[i].location.filename
                            ? tokens[i].location.filename : opts.filename,
                        tokens[i].location.line,
                        tokens[i].location.column,
                        tokens[i].lexeme ? tokens[i].lexeme : "?");
                lex_err = true;
            }
        }
        if (lex_err) { exit_code = 1; goto cleanup; }
    }

    /* ── Stage 2: Parse ─────────────────────────────────────────── */
    {
        Parser     *parser  = parser_create(tokens, token_count, opts.filename, errors);
        ProgramNode *program = parser_parse(parser);
        parser_free(parser);

        if (!check_errors(errors, "Parse errors")) {
            exit_code = 1;
            if (program) ast_free((ASTNode *)program);
            goto cleanup;
        }
        if (!program) {
            fprintf(stderr, "ocl: parser returned NULL without errors — internal error\n");
            exit_code = 1;
            goto cleanup;
        }

        /* ── Stage 3: Type-check ──────────────────────────────── */
        if (!opts.skip_typecheck) {
            TypeChecker *tc    = type_checker_create(errors);
            bool         tc_ok = type_checker_check(tc, program);
            type_checker_free(tc);

            if (!tc_ok || !check_errors(errors, "Type errors")) {
                exit_code = 1;
                ast_free((ASTNode *)program);
                goto cleanup;
            }
        }

        /* ── Stage 4: Code generation ─────────────────────────── */
        Bytecode      *bytecode = bytecode_create();
        CodeGenerator *gen      = codegen_create(errors);
        bool           cg_ok   = codegen_generate(gen, program, bytecode);
        codegen_free(gen);

        ast_free((ASTNode *)program);

        if (!cg_ok || !check_errors(errors, "Code generation errors")) {
            exit_code = 1;
            bytecode_free(bytecode);
            goto cleanup;
        }

        if (opts.dump_bytecode) {
            bytecode_dump(bytecode);
            bytecode_free(bytecode);
            goto cleanup;
        }

        /* ── Stage 5: Execute ─────────────────────────────────── */
        VM *vm = vm_create(bytecode);
        if (!vm) {
            fprintf(stderr, "ocl: failed to create VM\n");
            exit_code = 1;
            bytecode_free(bytecode);
            goto cleanup;
        }

        struct timespec t0 = {0}, t1 = {0};
        if (opts.show_time) clock_gettime(CLOCK_MONOTONIC, &t0);

        exit_code = vm_execute(vm);

        if (opts.show_time) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = (t1.tv_sec - t0.tv_sec) * 1e6
                      + (t1.tv_nsec - t0.tv_nsec) / 1e3;
            if      (us < 1000.0) fprintf(stderr, "[time: %.1f µs]\n", us);
            else if (us < 1e6)    fprintf(stderr, "[time: %.2f ms]\n", us / 1000.0);
            else                  fprintf(stderr, "[time: %.3f s]\n",  us / 1e6);
        }

        vm_free(vm);
        bytecode_free(bytecode);
    }

cleanup:
    tokens_free(tokens, token_count);
    error_collector_free(errors);
    ocl_free(source);
    return exit_code;
}
