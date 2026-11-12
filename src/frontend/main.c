#define _POSIX_C_SOURCE 200809L

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

/* ── CLI options ──── */
typedef struct {
    const char *filename;
    const char *output;        /* -o/--output: target for --compile        */
    bool        compile_only;  /* -c/--compile: write .ocx and exit        */
    bool        dump_tokens;
    bool        dump_bytecode;
    bool        skip_typecheck;
    bool        show_time;
} Options;

/* ── Helpers ──── */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <file.ocl | file.ocx>\n"
        "\n"
        "Modes:\n"
        "  file.ocl          Interpret OCL source through the full pipeline:\n"
        "                    lex -> parse -> typecheck -> codegen -> run\n"
        "  file.ocx          Run a compiled OCL bytecode executable directly\n"
        "                    (front end skipped — this is the CoreSX runtime path)\n"
        "\n"
        "Compiler options:\n"
        "  -c, --compile     Compile only: emit a .ocx bytecode executable, do not run\n"
        "  -o, --output <f>  Output path for --compile (default: <input>.ocx)\n"
        "\n"
        "Options:\n"
        "  --dump-tokens     Print lexer tokens and exit (source input only)\n"
        "  --dump-bytecode   Print bytecode disassembly and exit\n"
        "  --no-typecheck    Skip the type checker\n"
        "  --time            Report execution time\n"
        "  -h, --help        Show this help\n",
        prog);
}

/* True if `path` ends in ".ocx" (a compiled OCL bytecode executable). */
static bool has_ocx_ext(const char *path) {
    size_t len = path ? strlen(path) : 0;
    return len >= 4 && !strcmp(path + len - 4, ".ocx");
}

/* "dir/program.ocl" -> "dir/program.ocx" (malloc'd); appends when no extension. */
static char *default_output_path(const char *input) {
    const char *slash = strrchr(input, '/');
    const char *dot   = strrchr(input, '.');
    size_t base = (dot && dot > input && (!slash || dot > slash))
                ? (size_t)(dot - input) : strlen(input);
    char *out = ocl_malloc(base + 5);
    memcpy(out, input, base);
    memcpy(out + base, ".ocx", 5);
    return out;
}

/* Create a VM, run the bytecode to completion, and report --time. */
static int run_vm(Bytecode *bytecode, bool show_time) {
    VM *vm = vm_create(bytecode);
    if (!vm) {
        fprintf(stderr, "ocl: failed to create VM\n");
        return 1;
    }

    struct timespec t0 = {0}, t1 = {0};
    if (show_time) clock_gettime(CLOCK_MONOTONIC, &t0);

    int exit_code = vm_execute(vm);

    if (show_time) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = (t1.tv_sec - t0.tv_sec) * 1e6
                + (t1.tv_nsec - t0.tv_nsec) / 1e3;
        if      (us < 1000.0) fprintf(stderr, "[time: %.1f µs]\n", us);
        else if (us < 1e6)    fprintf(stderr, "[time: %.2f ms]\n", us / 1000.0);
        else                  fprintf(stderr, "[time: %.3f s]\n",  us / 1e6);
    }

    vm_free(vm);
    return exit_code;
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
        else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compile"))
            out->compile_only = true;
        else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ocl: option '%s' requires a file name\n", argv[i]);
                return false;
            }
            out->output = argv[++i];
        }
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
    if (out->compile_only && has_ocx_ext(out->filename)) {
        fprintf(stderr, "ocl: '%s' is already compiled bytecode — "
                "--compile needs OCL source\n", out->filename);
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

/* ── Entry point ──── */

int main(int argc, char *argv[]) {
    ocl_set_program_path(argv[0]);

    Options opts;
    if (!parse_options(argc, argv, &opts))
        return 1;

    /* ════
       Mode 1: run a pre-compiled .ocx bytecode executable.
       No lexer, parser, type checker or code generator needed — this is
       the minimal runtime path for CoreSX.
       ════ */
    if (has_ocx_ext(opts.filename)) {
        if (opts.dump_tokens) {
            fprintf(stderr, "ocl: --dump-tokens requires OCL source, not compiled bytecode\n");
            return 1;
        }
        Bytecode *bytecode = bytecode_load(opts.filename);
        if (!bytecode) return 1;

        if (opts.dump_bytecode) {
            bytecode_dump(bytecode);
            bytecode_free(bytecode);
            return 0;
        }

        int exit_code = run_vm(bytecode, opts.show_time);
        bytecode_free(bytecode);
        return exit_code;
    }

    /* ════
       Mode 2: OCL source — full pipeline.
       ════ */
    char *source = read_file(opts.filename);
    if (!source) return 1;

    int             exit_code    = 0;
    ErrorCollector *errors       = error_collector_create();
    Token          *tokens       = NULL;
    size_t          token_count  = 0;
    Bytecode       *bytecode     = NULL;

    /* ── Stage 1: Lex ──── */
    Lexer *lexer = lexer_create(source, opts.filename);
    tokens       = lexer_tokenize_all(lexer, &token_count);
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

    /* ── Stage 2: Parse ──── */
    {
        Parser      *parser  = parser_create(tokens, token_count, opts.filename, errors);
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

        /* ── Stage 3: Type-check ──── */
        if (!opts.skip_typecheck) {
            TypeChecker *tc    = type_checker_create(errors);
            bool         tc_ok = type_checker_check(tc, program);
            type_checker_free(tc);

            bool has_type_errors = !check_errors(errors, "Type errors");
            if (!tc_ok || has_type_errors) {
                exit_code = 1;
                ast_free((ASTNode *)program);
                goto cleanup;
            }
        }

        /* ── Stage 4: Code generation ──── */
        bytecode      = bytecode_create();
        CodeGenerator *gen    = codegen_create(errors);
        bool           cg_ok = codegen_generate(gen, program, bytecode);
        codegen_free(gen);

        ast_free((ASTNode *)program);

        if (!cg_ok || error_has_errors(errors)) {
            check_errors(errors, "Code generation errors");
            exit_code = 1;
            goto cleanup;
        }

        if (opts.dump_bytecode) bytecode_dump(bytecode);

        /* ── Stage 5a: Compile only — write the .ocx executable ──── */
        if (opts.compile_only) {
            char  *auto_out = NULL;
            const char *out = opts.output;
            if (!out) {
                auto_out = default_output_path(opts.filename);
                out      = auto_out;
            }
            if (bytecode_save(bytecode, out)) {
                printf("Compiled %s -> %s (%zu instructions, %zu constants, %zu functions)\n",
                       opts.filename, out,
                       bytecode->instruction_count,
                       bytecode->constant_count,
                       bytecode->function_count);
            } else {
                exit_code = 1;
            }
            ocl_free(auto_out);
            goto cleanup;
        }

        /* ── Stage 5b: Execute ──── */
        exit_code = run_vm(bytecode, opts.show_time);
    }

cleanup:
    tokens_free(tokens, token_count);
    error_collector_free(errors);
    bytecode_free(bytecode);
    ocl_free(source);
    return exit_code;
}
