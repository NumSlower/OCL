#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define TokenType WindowsTokenType
#include <windows.h>
#undef TokenType
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "codegen.h"
#include "bytecode.h"
#include "elf_emitter.h"
#include "vm.h"

/* ── CLI options ──────────────────────────────────────────────────── */
typedef struct {
    const char *filename;
    bool        run_exec;
    bool        watch;
    bool        dump_tokens;
    bool        dump_bytecode;
    bool        check_only;
    bool        skip_typecheck;
    bool        show_time;
    const char *emit_exec;
    const char *emit_elf;
    int         program_argc;
    char      **program_argv;
} Options;

typedef struct {
    char  **paths;
    size_t  count;
    size_t  capacity;
} WatchSet;

static bool check_errors(ErrorCollector *ec, const char *phase_desc);
static void print_program_result(const VM *vm);

/* ── Helpers ──────────────────────────────────────────────────────── */

#if defined(_WIN32)
#define OCL_EXEC_EXT ".exe"
#else
#define OCL_EXEC_EXT ".elf"
#endif

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <file.ocl> [-- args...]\n"
        "       %s [options] -r <file" OCL_EXEC_EXT "> [-- args...]\n"
        "\n"
        "Options:\n"
        "  --dump-tokens    Print lexer tokens and exit\n"
        "  --dump-bytecode  Print bytecode disassembly and exit\n"
        "  --check          Run through parsing and type checking, then exit\n"
        "  --no-typecheck   Skip the type checker\n"
        "  --watch          Re-run when the source or imported files change\n"
        "  --time           Report execution time\n"
        "  -r FILE          Run a compiled OCL executable\n"
        "  -e NAME          Compile to NAME" OCL_EXEC_EXT " in the current directory\n"
        "  --emit-elf PATH  Compile the input file to a NumOS ELF executable\n"
        "  --               Pass remaining values to terminal.args()\n"
        "  -h, --help       Show this help\n",
        prog, prog);
}

static bool has_suffix_ci(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t tlen = strlen(suffix);
    if (slen < tlen) return false;
    s += slen - tlen;
    for (size_t i = 0; i < tlen; i++) {
        char a = s[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool build_exec_output_path(const char *name, char *out, size_t cap) {
    if (!name || !out || cap == 0) return false;
    int n;
    if (has_suffix_ci(name, ".elf") || has_suffix_ci(name, ".exe"))
        n = snprintf(out, cap, "%s", name);
    else
        n = snprintf(out, cap, "%s" OCL_EXEC_EXT, name);
    return n >= 0 && (size_t)n < cap;
}

static char *read_file(const char *path, ErrorCollector *errors) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errors) {
            error_add(errors, ERRK_OPERATION, ERROR_LEXER, LOC_NONE,
                      "cannot open '%s': %s", path, strerror(errno));
        }
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        if (errors) {
            error_add(errors, ERRK_OPERATION, ERROR_LEXER, LOC_NONE,
                      "failed to seek '%s'", path);
        }
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        if (errors) {
            error_add(errors, ERRK_OPERATION, ERROR_LEXER, LOC_NONE,
                      "failed to tell size of '%s'", path);
        }
        fclose(f);
        return NULL;
    }
    rewind(f);

    char  *buf = ocl_malloc((size_t)size + 1);
    size_t rd  = fread(buf, 1, (size_t)size, f);
    buf[rd] = '\0';

    if (ferror(f)) {
        if (errors) {
            error_add(errors, ERRK_OPERATION, ERROR_LEXER, LOC_NONE,
                      "read error on '%s'", path);
        }
        ocl_free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

static void watch_set_free(WatchSet *set) {
    if (!set) return;
    for (size_t i = 0; i < set->count; i++)
        ocl_free(set->paths[i]);
    ocl_free(set->paths);
    set->paths = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void watch_set_add(WatchSet *set, const char *path) {
    if (!set || !path || !path[0]) return;
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->paths[i], path) == 0)
            return;
    }
    if (set->count >= set->capacity) {
        set->capacity = set->capacity ? set->capacity * 2 : 8;
        set->paths = ocl_realloc(set->paths, set->capacity * sizeof(char *));
    }
    set->paths[set->count++] = ocl_strdup(path);
}

static void collect_watch_paths(ProgramNode *program, WatchSet *set) {
    if (!program || !set) return;
    if (program->module_path)
        watch_set_add(set, program->module_path);
    for (size_t i = 0; i < program->import_count; i++)
        collect_watch_paths(program->imports[i], set);
}

static bool file_timestamp_ms(const char *path, uint64_t *out_ms) {
    if (!path || !out_ms) return false;
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return false;
    ULARGE_INTEGER value;
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    *out_ms = value.QuadPart / 10000ULL;
    return true;
#else
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
#if defined(__APPLE__)
    *out_ms = (uint64_t)st.st_mtimespec.tv_sec * 1000ULL +
              (uint64_t)st.st_mtimespec.tv_nsec / 1000000ULL;
#else
    *out_ms = (uint64_t)st.st_mtim.tv_sec * 1000ULL +
              (uint64_t)st.st_mtim.tv_nsec / 1000000ULL;
#endif
    return true;
#endif
}

static void watch_sleep_ms(unsigned int ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000U);
#endif
}

static void wait_for_watch_change(const WatchSet *set) {
    uint64_t *timestamps;

    if (!set || set->count == 0) return;
    timestamps = ocl_malloc(set->count * sizeof(uint64_t));
    for (size_t i = 0; i < set->count; i++) {
        if (!file_timestamp_ms(set->paths[i], &timestamps[i]))
            timestamps[i] = 0;
    }

    for (;;) {
        watch_sleep_ms(250);
        for (size_t i = 0; i < set->count; i++) {
            uint64_t current = 0;
            if (!file_timestamp_ms(set->paths[i], &current))
                current = 0;
            if (current != timestamps[i]) {
                ocl_free(timestamps);
                return;
            }
        }
    }
}

static int run_source_file_once(const Options *opts, WatchSet *watch_paths) {
    int exit_code = 0;
    ErrorCollector *errors = error_collector_create();
    char *source = NULL;
    Token *tokens = NULL;
    size_t token_count = 0;

    source = read_file(opts->filename, errors);
    if (!source) {
        error_print_all(errors);
        error_collector_free(errors);
        return 1;
    }

    Lexer *lexer = lexer_create(source, opts->filename);
    tokens = lexer_tokenize_all(lexer, &token_count);
    lexer_free(lexer);

    bool lex_err = false;
    for (size_t i = 0; i < token_count; i++) {
        if (tokens[i].type == TOKEN_ERROR) {
            SourceLocation loc = tokens[i].location;
            if (!loc.filename) loc.filename = opts->filename;
            error_add(errors, ERRK_SYNTAX, ERROR_LEXER, loc,
                      "unexpected character '%s'", tokens[i].lexeme ? tokens[i].lexeme : "?");
            lex_err = true;
        }
    }

    if (opts->dump_tokens) {
        for (size_t i = 0; i < token_count; i++) {
            printf("[%4zu] line %-4d type %-20d  '%s'\n",
                   i,
                   tokens[i].location.line,
                   tokens[i].type,
                   tokens[i].lexeme ? tokens[i].lexeme : "");
        }
        if (lex_err) {
            error_print_all(errors);
            exit_code = 1;
        }
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return exit_code;
    }

    if (lex_err) {
        error_print_all(errors);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 1;
    }

    Parser *parser = parser_create(tokens, token_count, opts->filename, errors);
    ProgramNode *program = parser_parse(parser);
    parser_free(parser);

    if (!check_errors(errors, "Parse errors")) {
        exit_code = 1;
        if (program) ast_free((ASTNode *)program);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return exit_code;
    }
    if (!program) {
        error_add(errors, ERRK_LOGIC, ERROR_PARSER, LOC_NONE,
                  "parser returned NULL without errors (internal error)");
        error_print_all(errors);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 1;
    }

    if (!opts->skip_typecheck) {
        TypeChecker *tc = type_checker_create(errors);
        bool tc_ok = type_checker_check(tc, program);
        type_checker_free(tc);
        if (!check_errors(errors, "Type errors")) {
            ast_free((ASTNode *)program);
            tokens_free(tokens, token_count);
            ocl_free(source);
            error_collector_free(errors);
            return 1;
        }
        if (!tc_ok) {
            error_add(errors, ERRK_LOGIC, ERROR_TYPE_CHECKER, LOC_NONE,
                      "type checker failed without producing diagnostics (internal error)");
            error_print_all(errors);
            ast_free((ASTNode *)program);
            tokens_free(tokens, token_count);
            ocl_free(source);
            error_collector_free(errors);
            return 1;
        }
    }

        if (watch_paths) {
            watch_set_free(watch_paths);
            collect_watch_paths(program, watch_paths);
        }

        if (opts->check_only) {
            ast_free((ASTNode *)program);
            tokens_free(tokens, token_count);
            ocl_free(source);
            error_collector_free(errors);
            return 0;
        }

        Bytecode *bytecode = bytecode_create();
        CodeGenerator *gen = codegen_create(errors);
    bool cg_ok = codegen_generate(gen, program, bytecode);
    codegen_free(gen);
    ast_free((ASTNode *)program);

    if (!check_errors(errors, "Code generation errors")) {
        bytecode_free(bytecode);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 1;
    }
    if (!cg_ok) {
        error_add(errors, ERRK_LOGIC, ERROR_CODEGEN, LOC_NONE,
                  "code generation failed without producing diagnostics (internal error)");
        error_print_all(errors);
        bytecode_free(bytecode);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 1;
    }

    if (opts->dump_bytecode) {
        bytecode_dump(bytecode);
        bytecode_free(bytecode);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 0;
    }

    if (opts->emit_elf) {
        if (!elf_emit_binary(bytecode, opts->emit_elf, errors)) {
            error_print_all(errors);
            exit_code = 1;
        }
        bytecode_free(bytecode);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return exit_code;
    }

    if (opts->emit_exec) {
        char out_path[4096];
        if (!build_exec_output_path(opts->emit_exec, out_path, sizeof(out_path)) ||
            !bytecode_write_standalone(bytecode, out_path)) {
            fprintf(stderr, "ocl: failed to write executable '%s'\n", opts->emit_exec);
            exit_code = 1;
        } else {
            fprintf(stdout, "Built compiled executable: %s\n", out_path);
        }
        bytecode_free(bytecode);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return exit_code;
    }

    VM *vm = vm_create(bytecode, errors);
    if (!vm) {
        error_add(errors, ERRK_LOGIC, ERROR_RUNTIME, LOC_NONE, "failed to create VM");
        error_print_all(errors);
        bytecode_free(bytecode);
        tokens_free(tokens, token_count);
        ocl_free(source);
        error_collector_free(errors);
        return 1;
    }
    vm_set_program_args(vm, opts->program_argc, (const char *const *)opts->program_argv);

    double start_ms = 0.0;
    if (opts->show_time) start_ms = ocl_monotonic_time_ms();

    exit_code = vm_execute(vm);
    if (error_has_errors(errors)) {
        error_print_all(errors);
    } else {
        print_program_result(vm);
    }

    if (opts->show_time) {
        double elapsed_ms = ocl_monotonic_time_ms() - start_ms;
        double us = elapsed_ms * 1000.0;
        if      (us < 1000.0) fprintf(stderr, "[time: %.1f us]\n", us);
        else if (us < 1e6)    fprintf(stderr, "[time: %.2f ms]\n", us / 1000.0);
        else                  fprintf(stderr, "[time: %.3f s]\n",  us / 1e6);
    }

    vm_free(vm);
    bytecode_free(bytecode);
    tokens_free(tokens, token_count);
    ocl_free(source);
    error_collector_free(errors);
    return exit_code;
}

static bool parse_options(int argc, char *argv[], Options *out) {
    *out = (Options){0};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) {
            out->program_argc = argc - i - 1;
            out->program_argv = argv + i + 1;
            break;
        }
        if (!strcmp(argv[i], "--dump-tokens"))        out->dump_tokens   = true;
        else if (!strcmp(argv[i], "--dump-bytecode")) out->dump_bytecode = true;
        else if (!strcmp(argv[i], "--check"))         out->check_only    = true;
        else if (!strcmp(argv[i], "--no-typecheck"))  out->skip_typecheck = true;
        else if (!strcmp(argv[i], "--watch"))         out->watch         = true;
        else if (!strcmp(argv[i], "--time"))          out->show_time     = true;
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--run-exec")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ocl: -r requires an executable path\n");
                return false;
            }
            if (out->filename) {
                fprintf(stderr, "ocl: multiple input files specified\n");
                return false;
            }
            out->filename = argv[++i];
            out->run_exec = true;
        }
        else if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--emit-exec")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ocl: -e requires an output name\n");
                return false;
            }
            out->emit_exec = argv[++i];
        }
        else if (!strcmp(argv[i], "--emit-elf")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ocl: --emit-elf requires an output path\n");
                return false;
            }
            out->emit_elf = argv[++i];
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
    return true;
}

static bool check_errors(ErrorCollector *ec, const char *phase_desc) {
    if (!error_has_errors(ec)) return true;
    (void)phase_desc;
    error_print_all(ec);
    error_collector_reset(ec); /* avoid double-printing later */
    return false;
}

static void print_program_result(const VM *vm) {
    if (!vm || !vm->has_result || vm->result.type == VALUE_NULL)
        return;
    puts(value_to_string(vm->result));
}

/* ── Entry point ──────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* Check for embedded self-payload (standalone executable mode) */
    {
        Bytecode *self_bc = bytecode_read_self_payload();
        if (self_bc) {
            int exit_code = 0;
            ErrorCollector *errors = error_collector_create();
            VM *vm = vm_create(self_bc, errors);
            if (!vm) {
                fprintf(stderr, "ocl: failed to create VM\n");
                bytecode_free(self_bc);
                error_collector_free(errors);
                return 1;
            }

            /* Support "exe -- arg1 arg2" for passing program arguments */
            int prog_argc = 0;
            char **prog_argv = NULL;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "--") == 0) {
                    prog_argc = argc - i - 1;
                    prog_argv = argv + i + 1;
                    break;
                }
            }
            vm_set_program_args(vm, prog_argc, (const char *const *)prog_argv);

            exit_code = vm_execute(vm);
            if (error_has_errors(errors))
                error_print_all(errors);
            else if (vm->has_result && vm->result.type != VALUE_NULL)
                puts(value_to_string(vm->result));

            vm_free(vm);
            bytecode_free(self_bc);
            error_collector_free(errors);
            return exit_code;
        }
    }

    Options opts;
    if (!parse_options(argc, argv, &opts))
        return 1;
    if (opts.emit_exec && opts.emit_elf) {
        fprintf(stderr, "ocl: use either -e or --emit-elf, not both\n");
        return 1;
    }
    if (opts.check_only && opts.skip_typecheck) {
        fprintf(stderr, "ocl: --check cannot be combined with --no-typecheck\n");
        return 1;
    }
    if (opts.check_only && opts.dump_tokens) {
        fprintf(stderr, "ocl: use either --check or --dump-tokens\n");
        return 1;
    }
    if (opts.check_only && (opts.dump_bytecode || opts.run_exec || opts.emit_exec || opts.emit_elf)) {
        fprintf(stderr, "ocl: --check only works when type checking source files\n");
        return 1;
    }
    if (opts.watch && (opts.run_exec || opts.emit_exec || opts.emit_elf)) {
        fprintf(stderr, "ocl: --watch only works when running source files\n");
        return 1;
    }
    if (opts.run_exec && (opts.emit_exec || opts.emit_elf)) {
        fprintf(stderr, "ocl: -r cannot be combined with build options\n");
        return 1;
    }

    int             exit_code = 0;
    ErrorCollector *errors    = error_collector_create();
    char           *source    = NULL;
    Token          *tokens    = NULL;
    size_t          token_count = 0;
    Bytecode       *loaded_bytecode = NULL;
    bool            is_compiled_exec = false;

    loaded_bytecode = bytecode_read_executable(opts.filename, &is_compiled_exec);
    if (!loaded_bytecode && is_compiled_exec) {
        error_add(errors, ERRK_OPERATION, ERROR_RUNTIME, LOC_NONE,
                  "failed to load compiled executable '%s'", opts.filename);
        error_print_all(errors);
        error_collector_reset(errors);
        exit_code = 1;
        goto cleanup;
    }
    if (!loaded_bytecode && opts.run_exec) {
        error_add(errors, ERRK_OPERATION, ERROR_RUNTIME, LOC_NONE,
                  "'%s' is not a compiled executable", opts.filename);
        error_print_all(errors);
        error_collector_reset(errors);
        exit_code = 1;
        goto cleanup;
    }

    if (loaded_bytecode) {
        if (opts.dump_tokens) {
            error_add(errors, ERRK_OPERATION, ERROR_RUNTIME, LOC_NONE,
                      "--dump-tokens is not supported for compiled executables");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            bytecode_free(loaded_bytecode);
            goto cleanup;
        }
        if (opts.check_only) {
            error_add(errors, ERRK_OPERATION, ERROR_RUNTIME, LOC_NONE,
                      "--check is not supported for compiled executables");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            bytecode_free(loaded_bytecode);
            goto cleanup;
        }
        if (opts.emit_exec || opts.emit_elf) {
            error_add(errors, ERRK_OPERATION, ERROR_RUNTIME, LOC_NONE,
                      "recompiling a compiled executable is not supported");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            bytecode_free(loaded_bytecode);
            goto cleanup;
        }

        if (opts.dump_bytecode) {
            bytecode_dump(loaded_bytecode);
            bytecode_free(loaded_bytecode);
            goto cleanup;
        }

        VM *vm = vm_create(loaded_bytecode, errors);
        if (!vm) {
            error_add(errors, ERRK_LOGIC, ERROR_RUNTIME, LOC_NONE,
                      "failed to create VM");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            bytecode_free(loaded_bytecode);
            goto cleanup;
        }
        vm_set_program_args(vm, opts.program_argc, (const char *const *)opts.program_argv);

        double start_ms = 0.0;
        if (opts.show_time) start_ms = ocl_monotonic_time_ms();

        exit_code = vm_execute(vm);
        if (error_has_errors(errors)) {
            error_print_all(errors);
            error_collector_reset(errors);
        } else {
            print_program_result(vm);
        }

        if (opts.show_time) {
            double elapsed_ms = ocl_monotonic_time_ms() - start_ms;
            double us = elapsed_ms * 1000.0;
            if      (us < 1000.0) fprintf(stderr, "[time: %.1f us]\n", us);
            else if (us < 1e6)    fprintf(stderr, "[time: %.2f ms]\n", us / 1000.0);
            else                  fprintf(stderr, "[time: %.3f s]\n",  us / 1e6);
        }

        vm_free(vm);
        bytecode_free(loaded_bytecode);
        goto cleanup;
    }

    if (opts.watch) {
        WatchSet watch_paths = {0};
        int last_exit_code = 0;
        for (;;) {
            last_exit_code = run_source_file_once(&opts, &watch_paths);
            fprintf(stderr, "[watch] waiting for changes...\n");
            wait_for_watch_change(&watch_paths);
            fprintf(stderr, "[watch] change detected. Re-running %s\n", opts.filename);
        }
        watch_set_free(&watch_paths);
        exit_code = last_exit_code;
        goto cleanup;
    }

    source = read_file(opts.filename, errors);

    if (!source) {
        error_print_all(errors);
        error_collector_reset(errors);
        exit_code = 1;
        goto cleanup;
    }

    /* ── Stage 1: Lex ───────────────────────────────────────────── */
    Lexer  *lexer       = lexer_create(source, opts.filename);
    tokens              = lexer_tokenize_all(lexer, &token_count);
    lexer_free(lexer);

    bool lex_err = false;
    for (size_t i = 0; i < token_count; i++) {
        if (tokens[i].type == TOKEN_ERROR) {
            SourceLocation loc = tokens[i].location;
            if (!loc.filename) loc.filename = opts.filename;
            error_add(errors, ERRK_SYNTAX, ERROR_LEXER, loc,
                      "unexpected character '%s'", tokens[i].lexeme ? tokens[i].lexeme : "?");
            lex_err = true;
        }
    }

    if (opts.dump_tokens) {
        for (size_t i = 0; i < token_count; i++) {
            printf("[%4zu] line %-4d type %-20d  '%s'\n",
                   i,
                   tokens[i].location.line,
                   tokens[i].type,
                   tokens[i].lexeme ? tokens[i].lexeme : "");
        }
        if (lex_err) {
            error_print_all(errors);
            exit_code = 1;
        }
        goto cleanup;
    }

    if (lex_err) {
        error_print_all(errors);
        exit_code = 1;
        /* Clear errors so optional leak reporting doesn't re-print them. */
        error_collector_reset(errors);
        goto cleanup;
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
            error_add(errors, ERRK_LOGIC, ERROR_PARSER, LOC_NONE,
                      "parser returned NULL without errors (internal error)");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            goto cleanup;
        }

        /* ── Stage 3: Type-check ──────────────────────────────── */
        if (!opts.skip_typecheck) {
            TypeChecker *tc    = type_checker_create(errors);
            bool         tc_ok = type_checker_check(tc, program);
            type_checker_free(tc);

            if (!check_errors(errors, "Type errors")) {
                exit_code = 1;
                ast_free((ASTNode *)program);
                goto cleanup;
            }
            if (!tc_ok) {
                error_add(errors, ERRK_LOGIC, ERROR_TYPE_CHECKER, LOC_NONE,
                          "type checker failed without producing diagnostics (internal error)");
                error_print_all(errors);
                error_collector_reset(errors);
                exit_code = 1;
                ast_free((ASTNode *)program);
                goto cleanup;
            }
        }

        /* ── Stage 4: Code generation ─────────────────────────── */
        if (opts.check_only) {
            ast_free((ASTNode *)program);
            goto cleanup;
        }

        Bytecode      *bytecode = bytecode_create();
        CodeGenerator *gen      = codegen_create(errors);
        bool           cg_ok   = codegen_generate(gen, program, bytecode);
        codegen_free(gen);

        ast_free((ASTNode *)program);

        if (!check_errors(errors, "Code generation errors")) {
            exit_code = 1;
            bytecode_free(bytecode);
            goto cleanup;
        }
        if (!cg_ok) {
            error_add(errors, ERRK_LOGIC, ERROR_CODEGEN, LOC_NONE,
                      "code generation failed without producing diagnostics (internal error)");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            bytecode_free(bytecode);
            goto cleanup;
        }

        if (opts.dump_bytecode) {
            bytecode_dump(bytecode);
            bytecode_free(bytecode);
            goto cleanup;
        }

        if (opts.emit_elf) {
            if (!elf_emit_binary(bytecode, opts.emit_elf, errors)) {
                error_print_all(errors);
                error_collector_reset(errors);
                exit_code = 1;
            } else {
                fprintf(stderr, "Built NumOS ELF executable: %s\n", opts.emit_elf);
            }
            bytecode_free(bytecode);
            goto cleanup;
        }

        if (opts.emit_exec) {
            char output_path[512];
            if (!build_exec_output_path(opts.emit_exec, output_path, sizeof(output_path))) {
                error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                          "output name is too long");
                error_print_all(errors);
                error_collector_reset(errors);
                exit_code = 1;
            } else if (!bytecode_write_standalone(bytecode, output_path)) {
                error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                          "failed to write compiled executable '%s'", output_path);
                error_print_all(errors);
                error_collector_reset(errors);
                exit_code = 1;
            } else {
                fprintf(stderr, "Built compiled executable: %s\n", output_path);
            }
            bytecode_free(bytecode);
            goto cleanup;
        }

        /* ── Stage 5: Execute ─────────────────────────────────── */
        VM *vm = vm_create(bytecode, errors);
        if (!vm) {
            error_add(errors, ERRK_LOGIC, ERROR_RUNTIME, LOC_NONE,
                      "failed to create VM");
            error_print_all(errors);
            error_collector_reset(errors);
            exit_code = 1;
            bytecode_free(bytecode);
            goto cleanup;
        }
        vm_set_program_args(vm, opts.program_argc, (const char *const *)opts.program_argv);

        double start_ms = 0.0;
        if (opts.show_time) start_ms = ocl_monotonic_time_ms();

        exit_code = vm_execute(vm);
        if (error_has_errors(errors)) {
            /* Runtime errors are collected by the VM and printed once here. */
            error_print_all(errors);
            error_collector_reset(errors);
        } else {
            print_program_result(vm);
        }

        if (opts.show_time) {
            double elapsed_ms = ocl_monotonic_time_ms() - start_ms;
            double us = elapsed_ms * 1000.0;
            if      (us < 1000.0) fprintf(stderr, "[time: %.1f us]\n", us);
            else if (us < 1e6)    fprintf(stderr, "[time: %.2f ms]\n", us / 1000.0);
            else                  fprintf(stderr, "[time: %.3f s]\n",  us / 1e6);
        }

        vm_free(vm);
        bytecode_free(bytecode);
    }

cleanup:
    tokens_free(tokens, token_count);
    ocl_free(source);

    if (ocl_memtrace_enabled()) {
        size_t bytes = 0;
        size_t leaks = ocl_memtrace_leak_count(&bytes);
        if (leaks > 0) {
            char details[1024];
            ocl_memtrace_snprint(details, sizeof(details), 10);
            error_add(errors, ERRK_MEMORY, ERROR_RUNTIME, LOC_NONE,
                      "leaked %llu allocation(s), %llu bytes (sample: %s)",
                      (unsigned long long)leaks,
                      (unsigned long long)bytes,
                      details[0] ? details : "none");
            error_print_all(errors);
            error_collector_reset(errors);
            if (exit_code == 0) exit_code = 1;
        }
        ocl_memtrace_shutdown();
    }

    error_collector_free(errors);
    return exit_code;
}
