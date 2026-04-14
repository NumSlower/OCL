#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define TokenType WindowsTokenType
#include <windows.h>
#undef TokenType
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <fcntl.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#include "common.h"
#include "math.h"
#include "ocl_stdlib.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "codegen.h"
#include "bytecode.h"
#include "vm.h"

/* ══════════════════════════════════════════════════════════════════
   Argument helpers
   ══════════════════════════════════════════════════════════════════ */

static Value *pop_args(VM *vm, int argc) {
    if (argc <= 0) return NULL;
    Value *args = ocl_malloc((size_t)argc * sizeof(Value));
    for (int i = argc - 1; i >= 0; i--)
        args[i] = vm_pop(vm);
    return args;
}

static void free_args(Value *args, int argc) {
    if (!args) return;
    for (int i = 0; i < argc; i++) value_free(args[i]);
    ocl_free(args);
}

static int g_host_quiet_depth = 0;

static bool host_output_quiet(void) {
    return g_host_quiet_depth > 0;
}

static void host_output_push_quiet(void) {
    g_host_quiet_depth++;
}

static void host_output_pop_quiet(void) {
    if (g_host_quiet_depth > 0) g_host_quiet_depth--;
}

#define REQUIRE_ARGS(vm, args, argc, needed, name) \
    do { \
        if ((argc) < (needed)) { \
            if (!host_output_quiet()) \
                fprintf(stderr, "ocl: %s requires %d argument(s), got %d\n", \
                        (name), (needed), (argc)); \
            free_args((args), (argc)); \
            vm_push((vm), value_null()); \
            return; \
        } \
    } while (0)

static char *host_read_file(const char *path, ErrorCollector *errors) {
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

    char *buf = ocl_malloc((size_t)size + 1);
    size_t rd = fread(buf, 1, (size_t)size, f);
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

static bool host_collect_lex_errors(Token *tokens, size_t token_count,
                                    ErrorCollector *errors, const char *filename) {
    bool lex_err = false;
    for (size_t i = 0; i < token_count; i++) {
        if (tokens[i].type == TOKEN_ERROR) {
            SourceLocation loc = tokens[i].location;
            if (!loc.filename) loc.filename = filename;
            error_add(errors, ERRK_SYNTAX, ERROR_LEXER, loc,
                      "unexpected character '%s'",
                      tokens[i].lexeme ? tokens[i].lexeme : "?");
            lex_err = true;
        }
    }
    return lex_err;
}

static double host_time_now_ms(void) {
    return ocl_monotonic_time_ms();
}

static bool host_path_is_dir(const char *path) {
    if (!path || !path[0]) return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool host_path_exists(const char *path) {
    if (!path || !path[0]) return false;
#if defined(_WIN32)
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

static char *host_path_join(const char *base, const char *name) {
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    size_t blen = strlen(base ? base : "");
    size_t nlen = strlen(name ? name : "");
    bool needs_sep = blen > 0 &&
                     base[blen - 1] != '/' &&
                     base[blen - 1] != '\\';
    char *out = ocl_malloc(blen + nlen + (needs_sep ? 2 : 1));
    memcpy(out, base, blen);
    if (needs_sep) out[blen++] = sep;
    memcpy(out + blen, name, nlen);
    out[blen + nlen] = '\0';
    return out;
}

static void host_list_files_recursive(const char *path, OclArray *out) {
    if (!path || !out) return;
    if (!host_path_exists(path)) {
        return;
    }
    if (!host_path_is_dir(path)) {
        ocl_array_push(out, value_string_copy(path));
        return;
    }

#if defined(_WIN32)
    char *pattern = host_path_join(path, "*");
    WIN32_FIND_DATAA find_data;
    HANDLE handle = FindFirstFileA(pattern, &find_data);
    ocl_free(pattern);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    do {
        const char *name = find_data.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char *child = host_path_join(path, name);
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            host_list_files_recursive(child, out);
        } else {
            ocl_array_push(out, value_string(child));
            child = NULL;
        }
        ocl_free(child);
    } while (FindNextFileA(handle, &find_data) != 0);

    FindClose(handle);
#else
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char *child = host_path_join(path, name);
        if (host_path_is_dir(child)) {
            host_list_files_recursive(child, out);
        } else {
            ocl_array_push(out, value_string(child));
            child = NULL;
        }
        ocl_free(child);
    }

    closedir(dir);
#endif
}

static bool host_measure_source_file(const char *path, double *out_ms) {
    ErrorCollector *errors = NULL;
    char *source = NULL;
    Token *tokens = NULL;
    size_t token_count = 0;
    Bytecode *bytecode = NULL;
    VM *child_vm = NULL;
    bool quiet_pushed = false;
    bool ok = false;

    if (!path || !out_ms) return false;

    errors = error_collector_create();
    if (!errors) return false;

    source = host_read_file(path, errors);
    if (!source) goto cleanup;

    Lexer *lexer = lexer_create(source, path);
    tokens = lexer_tokenize_all(lexer, &token_count);
    lexer_free(lexer);

    if (host_collect_lex_errors(tokens, token_count, errors, path)) goto cleanup;

    Parser *parser = parser_create(tokens, token_count, path, errors);
    ProgramNode *program = parser_parse(parser);
    parser_free(parser);

    if (!program || error_has_errors(errors)) {
        if (program) ast_free((ASTNode *)program);
        goto cleanup;
    }

    TypeChecker *tc = type_checker_create(errors);
    bool tc_ok = type_checker_check(tc, program);
    type_checker_free(tc);
    if (!tc_ok || error_has_errors(errors)) {
        ast_free((ASTNode *)program);
        goto cleanup;
    }

    bytecode = bytecode_create();
    CodeGenerator *gen = codegen_create(errors);
    bool cg_ok = codegen_generate(gen, program, bytecode);
    codegen_free(gen);
    ast_free((ASTNode *)program);
    if (!cg_ok || error_has_errors(errors)) goto cleanup;

    child_vm = vm_create(bytecode, errors);
    if (!child_vm) goto cleanup;

    double start_ms = host_time_now_ms();
    host_output_push_quiet();
    quiet_pushed = true;
    int exit_code = vm_execute(child_vm);
    host_output_pop_quiet();
    quiet_pushed = false;
    double end_ms = host_time_now_ms();

    if (exit_code != 0) {
        if (!error_has_errors(errors)) {
            error_add(errors, ERRK_OPERATION, ERROR_RUNTIME, LOC_NONE,
                      "timed run failed for '%s' with exit code %d",
                      path, exit_code);
        }
        goto cleanup;
    }
    if (error_has_errors(errors)) goto cleanup;

    *out_ms = end_ms - start_ms;
    ok = true;

cleanup:
    if (quiet_pushed)
        host_output_pop_quiet();
    if (!ok && errors && error_has_errors(errors))
        error_print_all(errors);
    if (child_vm) vm_free(child_vm);
    if (bytecode) bytecode_free(bytecode);
    tokens_free(tokens, token_count);
    ocl_free(source);
    error_collector_free(errors);
    return ok;
}

static int64_t to_int64(Value v) {
    switch (v.type) {
        case VALUE_INT:    return v.data.int_val;
        case VALUE_FLOAT:  return (int64_t)v.data.float_val;
        case VALUE_BOOL:   return v.data.bool_val ? 1 : 0;
        case VALUE_CHAR:   return (int64_t)(unsigned char)v.data.char_val;
        case VALUE_STRING: return v.data.string_val
                                  ? (int64_t)strtoll(v.data.string_val, NULL, 10)
                                  : 0;
        default: return 0;
    }
}

static double to_double(Value v) {
    switch (v.type) {
        case VALUE_INT:    return (double)v.data.int_val;
        case VALUE_FLOAT:  return v.data.float_val;
        case VALUE_BOOL:   return v.data.bool_val ? 1.0 : 0.0;
        case VALUE_CHAR:   return (double)(unsigned char)v.data.char_val;
        case VALUE_STRING: return v.data.string_val ? strtod(v.data.string_val, NULL) : 0.0;
        default: return 0.0;
    }
}

static const char *value_type_label(Value v) {
    switch (v.type) {
        case VALUE_NULL:   return "null";
        case VALUE_INT:    return "Int";
        case VALUE_FLOAT:  return "Float";
        case VALUE_STRING: return "String";
        case VALUE_BOOL:   return "Bool";
        case VALUE_CHAR:   return "Char";
        case VALUE_ARRAY:  return "Array";
        case VALUE_STRUCT: return "Struct";
        case VALUE_FUNCTION: return "Function";
    }
    return "Unknown";
}

static void append_bytes(char **buf, size_t *len, size_t *cap,
                         const char *src, size_t src_len) {
    if (!buf || !len || !cap || !src) return;
    if (*cap < *len + src_len + 1) {
        size_t new_cap = *cap ? *cap : 64;
        while (new_cap < *len + src_len + 1) new_cap *= 2;
        *buf = ocl_realloc(*buf, new_cap);
        *cap = new_cap;
    }
    memcpy(*buf + *len, src, src_len);
    *len += src_len;
    (*buf)[*len] = '\0';
}

static void append_cstr(char **buf, size_t *len, size_t *cap, const char *src) {
    append_bytes(buf, len, cap, src ? src : "", strlen(src ? src : ""));
}

static void append_char(char **buf, size_t *len, size_t *cap, char ch) {
    append_bytes(buf, len, cap, &ch, 1);
}

static void terminal_runtime_diag(const char *builtin_name, const char *fmt, ...) {
    va_list ap;

    if (!builtin_name || !fmt) return;
    fprintf(stderr, "ocl: %s: ", builtin_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void free_string_list(char **items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; i++)
        ocl_free(items[i]);
    ocl_free(items);
}

static char *dup_runtime_string(Value value) {
    const char *src;

    if (value.type == VALUE_STRING && value.data.string_val)
        src = value.data.string_val;
    else
        src = value_to_string(value);
    return ocl_strdup(src ? src : "");
}

static bool terminal_build_exec_argv(VM *vm, const char *builtin_name,
                                     Value program_value, Value args_value,
                                     char ***out_argv, size_t *out_count) {
    OclArray *args_array;
    char **argv;
    size_t argc;

    (void)vm;

    if (!out_argv || !out_count) return false;
    *out_argv = NULL;
    *out_count = 0;

    if (args_value.type != VALUE_ARRAY || !args_value.data.array_val) {
        terminal_runtime_diag(builtin_name, "processArgs must be an Array");
        return false;
    }

    args_array = args_value.data.array_val;
    argc = args_array->length + 1;
    argv = ocl_malloc((argc + 1) * sizeof(char *));
    for (size_t i = 0; i <= argc; i++)
        argv[i] = NULL;

    argv[0] = dup_runtime_string(program_value);
    if (!argv[0][0]) {
        terminal_runtime_diag(builtin_name, "program must not be empty");
        free_string_list(argv, argc + 1);
        return false;
    }

    for (size_t i = 0; i < args_array->length; i++)
        argv[i + 1] = dup_runtime_string(args_array->elements[i]);

    *out_argv = argv;
    *out_count = argc;
    return true;
}

static OclArray *terminal_make_result_array(int exit_code, char *output) {
    OclArray *result = ocl_array_new(2);
    ocl_array_push(result, value_int(exit_code));
    ocl_array_push(result, value_string(output ? output : ocl_strdup("")));
    return result;
}

static int OCL_UNUSED terminal_wait_status_to_exit_code(int status) {
#if defined(_WIN32)
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

#if defined(_WIN32)
static bool terminal_spawn_windows(const char *builtin_name, char **argv, size_t argc,
                                   bool capture_output, int *out_exit_code,
                                   char **out_output) {
    intptr_t spawn_result;
    char *captured_output = NULL;
    int exit_code = 0;
    int saved_stdout_fd = -1;
    int saved_stderr_fd = -1;
    int capture_fd = -1;
    char temp_path[MAX_PATH] = {0};
    char temp_file[MAX_PATH] = {0};
    bool ok = false;

    if (out_exit_code) *out_exit_code = 0;
    if (out_output) *out_output = NULL;
    if (!argv || argc == 0) {
        terminal_runtime_diag(builtin_name, "missing program");
        return false;
    }

    if (capture_output) {
        DWORD temp_len = GetTempPathA((DWORD)sizeof(temp_path), temp_path);
        if (temp_len == 0 || temp_len >= sizeof(temp_path)) {
            terminal_runtime_diag(builtin_name, "failed to resolve temporary directory (%lu)",
                                  (unsigned long)GetLastError());
            goto cleanup;
        }
        if (GetTempFileNameA(temp_path, "ocl", 0, temp_file) == 0) {
            terminal_runtime_diag(builtin_name, "failed to create temporary capture file (%lu)",
                                  (unsigned long)GetLastError());
            goto cleanup;
        }

        saved_stdout_fd = _dup(_fileno(stdout));
        saved_stderr_fd = _dup(_fileno(stderr));
        if (saved_stdout_fd < 0 || saved_stderr_fd < 0) {
            terminal_runtime_diag(builtin_name, "failed to duplicate standard handles");
            goto cleanup;
        }

        capture_fd = _open(temp_file, _O_CREAT | _O_TRUNC | _O_RDWR | _O_BINARY,
                           _S_IREAD | _S_IWRITE);
        if (capture_fd < 0) {
            terminal_runtime_diag(builtin_name, "failed to open capture file: %s",
                                  strerror(errno));
            goto cleanup;
        }

        fflush(stdout);
        fflush(stderr);
        if (_dup2(capture_fd, _fileno(stdout)) < 0 ||
            _dup2(capture_fd, _fileno(stderr)) < 0) {
            terminal_runtime_diag(builtin_name, "failed to redirect child output");
            goto cleanup;
        }
    }

    spawn_result = _spawnvp(_P_WAIT, argv[0], (const char * const *)argv);
    if (spawn_result == -1) {
        terminal_runtime_diag(builtin_name, "failed to start '%s': %s",
                              argv[0], strerror(errno));
        goto cleanup;
    }
    exit_code = (int)spawn_result;

    if (out_exit_code)
        *out_exit_code = exit_code;

    if (capture_output) {
        fflush(stdout);
        fflush(stderr);
        if (saved_stdout_fd >= 0) {
            _dup2(saved_stdout_fd, _fileno(stdout));
            _close(saved_stdout_fd);
            saved_stdout_fd = -1;
        }
        if (saved_stderr_fd >= 0) {
            _dup2(saved_stderr_fd, _fileno(stderr));
            _close(saved_stderr_fd);
            saved_stderr_fd = -1;
        }
        if (capture_fd >= 0) {
            _close(capture_fd);
            capture_fd = -1;
        }
        captured_output = host_read_file(temp_file, NULL);
    }

    if (out_output) {
        *out_output = captured_output ? captured_output : ocl_strdup("");
    } else {
        ocl_free(captured_output);
    }
    ok = true;

cleanup:
    if (capture_output) {
        fflush(stdout);
        fflush(stderr);
    }
    if (saved_stdout_fd >= 0) {
        _dup2(saved_stdout_fd, _fileno(stdout));
        _close(saved_stdout_fd);
    }
    if (saved_stderr_fd >= 0) {
        _dup2(saved_stderr_fd, _fileno(stderr));
        _close(saved_stderr_fd);
    }
    if (capture_fd >= 0)
        _close(capture_fd);
    if (temp_file[0])
        DeleteFileA(temp_file);
    if (!ok)
        ocl_free(captured_output);
    return ok;
}
#else
static bool set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static bool terminal_check_exec_error_pipe(const char *builtin_name, pid_t pid, int error_fd) {
    int child_errno = 0;
    ssize_t rd = read(error_fd, &child_errno, sizeof(child_errno));
    close(error_fd);

    if (rd == 0)
        return true;

    if (rd > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        terminal_runtime_diag(builtin_name, "failed to start process: %s",
                              strerror(child_errno));
        return false;
    }

    terminal_runtime_diag(builtin_name, "failed to read child startup state");
    waitpid(pid, NULL, 0);
    return false;
}

static bool terminal_spawn_posix(const char *builtin_name, char **argv, size_t argc,
                                 bool capture_output, int *out_exit_code,
                                 char **out_output) {
    int output_pipe[2] = { -1, -1 };
    int error_pipe[2] = { -1, -1 };
    char *captured_output = NULL;
    pid_t pid;
    int status = 0;
    bool ok = false;

    if (out_exit_code) *out_exit_code = 0;
    if (out_output) *out_output = NULL;
    if (!argv || argc == 0) {
        terminal_runtime_diag(builtin_name, "missing program");
        return false;
    }

    if (capture_output && pipe(output_pipe) != 0) {
        terminal_runtime_diag(builtin_name, "failed to create capture pipe: %s",
                              strerror(errno));
        return false;
    }
    if (pipe(error_pipe) != 0) {
        terminal_runtime_diag(builtin_name, "failed to create startup pipe: %s",
                              strerror(errno));
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        return false;
    }
    if (!set_cloexec(error_pipe[1])) {
        terminal_runtime_diag(builtin_name, "failed to configure startup pipe");
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        close(error_pipe[0]);
        close(error_pipe[1]);
        return false;
    }

    pid = fork();
    if (pid < 0) {
        terminal_runtime_diag(builtin_name, "failed to fork child process: %s",
                              strerror(errno));
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        close(error_pipe[0]);
        close(error_pipe[1]);
        return false;
    }

    if (pid == 0) {
        close(error_pipe[0]);

        if (capture_output) {
            close(output_pipe[0]);
            if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
                dup2(output_pipe[1], STDERR_FILENO) < 0) {
                int child_errno = errno;
                write(error_pipe[1], &child_errno, sizeof(child_errno));
                _exit(127);
            }
            close(output_pipe[1]);
        }

        execvp(argv[0], argv);

        {
            int child_errno = errno;
            write(error_pipe[1], &child_errno, sizeof(child_errno));
        }
        _exit(127);
    }

    close(error_pipe[1]);
    if (capture_output)
        close(output_pipe[1]);

    if (!terminal_check_exec_error_pipe(builtin_name, pid, error_pipe[0])) {
        if (capture_output)
            close(output_pipe[0]);
        return false;
    }

    if (capture_output) {
        size_t out_len = 0;
        size_t out_cap = 0;

        for (;;) {
            char chunk[4096];
            ssize_t rd = read(output_pipe[0], chunk, sizeof(chunk));

            if (rd < 0) {
                terminal_runtime_diag(builtin_name, "failed while reading child output: %s",
                                      strerror(errno));
                ocl_free(captured_output);
                close(output_pipe[0]);
                waitpid(pid, NULL, 0);
                return false;
            }
            if (rd == 0)
                break;
            append_bytes(&captured_output, &out_len, &out_cap, chunk, (size_t)rd);
        }

        close(output_pipe[0]);
    }

    if (waitpid(pid, &status, 0) < 0) {
        terminal_runtime_diag(builtin_name, "failed while waiting for child process: %s",
                              strerror(errno));
        return false;
    }

    if (out_exit_code)
        *out_exit_code = terminal_wait_status_to_exit_code(status);
    if (out_output)
        *out_output = captured_output ? captured_output : ocl_strdup("");
    else
        ocl_free(captured_output);
    ok = true;
    return ok;
}
#endif

static bool terminal_spawn_program(const char *builtin_name, char **argv, size_t argc,
                                   bool capture_output, int *out_exit_code,
                                   char **out_output) {
#if defined(_WIN32)
    return terminal_spawn_windows(builtin_name, argv, argc, capture_output,
                                  out_exit_code, out_output);
#else
    return terminal_spawn_posix(builtin_name, argv, argc, capture_output,
                                out_exit_code, out_output);
#endif
}

static bool terminal_spawn_shell(const char *builtin_name, const char *command,
                                 bool capture_output, int *out_exit_code,
                                 char **out_output) {
    char **argv = ocl_malloc(
#if defined(_WIN32)
        5 * sizeof(char *)
#else
        4 * sizeof(char *)
#endif
    );
    bool ok;

#if defined(_WIN32)
    argv[0] = ocl_strdup("powershell.exe");
    argv[1] = ocl_strdup("-NoProfile");
    argv[2] = ocl_strdup("-Command");
    argv[3] = ocl_strdup(command ? command : "");
    argv[4] = NULL;
#else
    argv[0] = ocl_strdup("/bin/sh");
    argv[1] = ocl_strdup("-c");
    argv[2] = ocl_strdup(command ? command : "");
    argv[3] = NULL;
#endif

    ok = terminal_spawn_program(builtin_name, argv,
#if defined(_WIN32)
                                4,
#else
                                3,
#endif
                                capture_output,
                                out_exit_code, out_output);
    free_string_list(argv,
#if defined(_WIN32)
                     5
#else
                     4
#endif
    );
    return ok;
}

static void builtin_terminal_args(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    OclArray *result = ocl_array_new((size_t)(vm && vm->program_argc > 0 ? vm->program_argc : 0));

    free_args(args, argc);

    if (vm) {
        for (int i = 0; i < vm->program_argc; i++)
            ocl_array_push(result, value_string_copy(vm->program_args[i]));
    }

    vm_push(vm, value_array(result));
    ocl_array_release(result);
}

static void builtin_terminal_exec(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    char **argv = NULL;
    size_t argv_count = 0;
    int exit_code = 0;
    bool ok;

    REQUIRE_ARGS(vm, args, argc, 2, "__terminalExec");
    if (!terminal_build_exec_argv(vm, "__terminalExec", args[0], args[1], &argv, &argv_count)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);

    if (host_output_quiet()) {
        char *captured = NULL;
        ok = terminal_spawn_program("__terminalExec", argv, argv_count, true, &exit_code, &captured);
        ocl_free(captured);
    } else {
        ok = terminal_spawn_program("__terminalExec", argv, argv_count, false, &exit_code, NULL);
    }

    free_string_list(argv, argv_count + 1);

    if (!ok) {
        vm_push(vm, value_null());
        return;
    }

    vm_push(vm, value_int(exit_code));
}

static void builtin_terminal_capture(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    char **argv = NULL;
    size_t argv_count = 0;
    int exit_code = 0;
    char *output = NULL;
    OclArray *result;

    REQUIRE_ARGS(vm, args, argc, 2, "__terminalCapture");
    if (!terminal_build_exec_argv(vm, "__terminalCapture", args[0], args[1], &argv, &argv_count)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);

    if (!terminal_spawn_program("__terminalCapture", argv, argv_count, true, &exit_code, &output)) {
        free_string_list(argv, argv_count + 1);
        vm_push(vm, value_null());
        return;
    }

    free_string_list(argv, argv_count + 1);

    result = terminal_make_result_array(exit_code, output);
    vm_push(vm, value_array(result));
    ocl_array_release(result);
}

static void builtin_terminal_shell(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    char *command = NULL;
    int exit_code = 0;
    bool ok;

    REQUIRE_ARGS(vm, args, argc, 1, "__terminalShell");
    command = dup_runtime_string(args[0]);
    free_args(args, argc);

    if (host_output_quiet()) {
        char *captured = NULL;
        ok = terminal_spawn_shell("__terminalShell", command, true, &exit_code, &captured);
        ocl_free(captured);
    } else {
        ok = terminal_spawn_shell("__terminalShell", command, false, &exit_code, NULL);
    }
    ocl_free(command);

    if (!ok) {
        vm_push(vm, value_null());
        return;
    }

    vm_push(vm, value_int(exit_code));
}

static void builtin_terminal_shell_capture(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    char *command = NULL;
    int exit_code = 0;
    char *output = NULL;
    OclArray *result;

    REQUIRE_ARGS(vm, args, argc, 1, "__terminalShellCapture");
    command = dup_runtime_string(args[0]);
    free_args(args, argc);

    if (!terminal_spawn_shell("__terminalShellCapture", command, true, &exit_code, &output)) {
        ocl_free(command);
        vm_push(vm, value_null());
        return;
    }
    ocl_free(command);

    result = terminal_make_result_array(exit_code, output);
    vm_push(vm, value_array(result));
    ocl_array_release(result);
}

static void builtin_terminal_os(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    const char *os_name = "unknown";

    free_args(args, argc);

#if defined(_WIN32)
    os_name = "windows";
#elif defined(__APPLE__)
    os_name = "macos";
#elif defined(__linux__)
    os_name = "linux";
#endif

    vm_push(vm, value_string_copy(os_name));
}

static double wrap_angle(double x) {
    const double pi = 3.14159265358979323846;
    const double tau = 6.28318530717958647692;

    while (x > pi) x -= tau;
    while (x < -pi) x += tau;
    return x;
}

static double approx_sin(double x) {
    double term;
    double sum;

    x = wrap_angle(x);
    term = x;
    sum = x;
    for (int i = 1; i < 8; i++) {
        term *= -x * x / ((double)(2 * i) * (double)(2 * i + 1));
        sum += term;
    }
    return sum;
}

static double approx_cos(double x) {
    double term = 1.0;
    double sum = 1.0;

    x = wrap_angle(x);
    for (int i = 1; i < 8; i++) {
        term *= -x * x / ((double)(2 * i - 1) * (double)(2 * i));
        sum += term;
    }
    return sum;
}

static double approx_tan(double x) {
    double c = approx_cos(x);
    if (c == 0.0) return 0.0;
    return approx_sin(x) / c;
}

static double approx_sqrt(double x) {
    double guess;

    if (x <= 0.0) return 0.0;
    guess = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 12; i++)
        guess = 0.5 * (guess + x / guess);
    return guess;
}

static void print_value(FILE *stream, Value v) {
    fputs(value_to_string(v), stream);
}

static void builtin_print(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);

    if (host_output_quiet()) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stdout);
        print_value(stdout, args[i]);
    }
    fputc('\n', stdout);
    fflush(stdout);
    free_args(args, argc);
    vm_push(vm, value_null());
}

static char *format_string_value(Value *args, int argc, const char *builtin_name) {
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;
    const char *fmt;
    int arg_index = 1;

    if (!args || argc <= 0) return ocl_strdup("");

    fmt = (args[0].type == VALUE_STRING && args[0].data.string_val)
        ? args[0].data.string_val
        : value_to_string(args[0]);

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%' || fmt[i + 1] == '\0') {
            append_char(&out, &len, &cap, fmt[i]);
            continue;
        }

        i++;
        if (fmt[i] == '%') {
            append_char(&out, &len, &cap, '%');
            continue;
        }

        if (arg_index >= argc) {
            append_char(&out, &len, &cap, '%');
            append_char(&out, &len, &cap, fmt[i]);
            continue;
        }

        Value arg = args[arg_index++];
        switch (fmt[i]) {
            case 'd':
            case 'i': {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%" PRId64, to_int64(arg));
                append_cstr(&out, &len, &cap, tmp);
                break;
            }
            case 'f': {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%g", to_double(arg));
                append_cstr(&out, &len, &cap, tmp);
                break;
            }
            case 'c': {
                char ch = (arg.type == VALUE_CHAR) ? arg.data.char_val : (char)to_int64(arg);
                append_char(&out, &len, &cap, ch);
                break;
            }
            case 's':
                if (arg.type == VALUE_STRING && arg.data.string_val)
                    append_cstr(&out, &len, &cap, arg.data.string_val);
                else
                    append_cstr(&out, &len, &cap, value_to_string(arg));
                break;
            case 'b':
                append_cstr(&out, &len, &cap, value_is_truthy(arg) ? "true" : "false");
                break;
            case 'v':
            default:
                append_cstr(&out, &len, &cap, value_to_string(arg));
                break;
        }
    }

    while (arg_index < argc) {
        if (len > 0) append_char(&out, &len, &cap, ' ');
        append_cstr(&out, &len, &cap, value_to_string(args[arg_index++]));
    }

    if (!out) return ocl_strdup("");
    (void)builtin_name;
    return out;
}

static void builtin_printf(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);

    REQUIRE_ARGS(vm, args, argc, 1, "printf");

    if (host_output_quiet()) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    char *out = format_string_value(args, argc, "printf");

    if (out) {
        fputs(out, stdout);
        ocl_free(out);
    }
    fflush(stdout);
    free_args(args, argc);
    vm_push(vm, value_null());
}

static void builtin_strformat(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "strFormat");
    char *out = format_string_value(args, argc, "strFormat");
    free_args(args, argc);
    vm_push(vm, value_string(out));
}

/* ══════════════════════════════════════════════════════════════════
   I/O
   ══════════════════════════════════════════════════════════════════ */

static void builtin_input(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);

    if (host_output_quiet()) {
        free_args(args, argc);
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }

    if (argc >= 1 && args[0].type == VALUE_STRING && args[0].data.string_val)
        fputs(args[0].data.string_val, stdout);
    fflush(stdout);
    free_args(args, argc);

    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) {
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

    vm_push(vm, value_string(ocl_strdup(buf)));
}

static void builtin_readline(VM *vm, int argc) {
    builtin_input(vm, argc);
}

/* ══════════════════════════════════════════════════════════════════
   Math
   ══════════════════════════════════════════════════════════════════ */

static void builtin_abs(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "abs");
    if (args[0].type == VALUE_INT)
        vm_push(vm, value_int(args[0].data.int_val < 0 ? -args[0].data.int_val : args[0].data.int_val));
    else
        vm_push(vm, value_float(fabs(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_sqrt(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "sqrt");
    vm_push(vm, value_float(approx_sqrt(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_pow(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "pow");
    vm_push(vm, value_float(pow(to_double(args[0]), to_double(args[1]))));
    free_args(args, argc);
}

static void builtin_sin(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "sin");
    vm_push(vm, value_float(approx_sin(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_cos(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "cos");
    vm_push(vm, value_float(approx_cos(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_tan(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "tan");
    vm_push(vm, value_float(approx_tan(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_floor_fn(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "floor");
    vm_push(vm, value_float(floor(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_ceil_fn(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "ceil");
    vm_push(vm, value_float(ceil(to_double(args[0]))));
    free_args(args, argc);
}

static void builtin_round_fn(VM *vm, int argc) {
    double x;
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "round");
    x = to_double(args[0]);
    vm_push(vm, value_float(x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5)));
    free_args(args, argc);
}

static void builtin_max(VM *vm, int argc) {
    double a, b;
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "max");
    a = to_double(args[0]);
    b = to_double(args[1]);
    if (args[0].type == VALUE_INT && args[1].type == VALUE_INT)
        vm_push(vm, value_int(a >= b ? (int64_t)a : (int64_t)b));
    else
        vm_push(vm, value_float(a >= b ? a : b));
    free_args(args, argc);
}

static void builtin_min(VM *vm, int argc) {
    double a, b;
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "min");
    a = to_double(args[0]);
    b = to_double(args[1]);
    if (args[0].type == VALUE_INT && args[1].type == VALUE_INT)
        vm_push(vm, value_int(a <= b ? (int64_t)a : (int64_t)b));
    else
        vm_push(vm, value_float(a <= b ? a : b));
    free_args(args, argc);
}

/* ══════════════════════════════════════════════════════════════════
   String functions
   ══════════════════════════════════════════════════════════════════ */

static void builtin_strlen(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "strLen");
    int64_t len = (args[0].type == VALUE_STRING && args[0].data.string_val)
                  ? (int64_t)strlen(args[0].data.string_val) : 0;
    free_args(args, argc);
    vm_push(vm, value_int(len));
}

static void builtin_substr(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "substr");

    if (args[0].type != VALUE_STRING) {
        free_args(args, argc);
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    const char *src  = args[0].data.string_val ? args[0].data.string_val : "";
    size_t      slen = strlen(src);
    int64_t     start  = to_int64(args[1]);
    int64_t     length = (argc >= 3) ? to_int64(args[2]) : (int64_t)slen;
    free_args(args, argc);

    if (start  < 0)      start  = 0;
    if (length < 0)      length = 0;
    if ((size_t)start >= slen) { vm_push(vm, value_string(ocl_strdup(""))); return; }
    if ((size_t)(start + length) > slen) length = (int64_t)(slen - (size_t)start);

    char *result = ocl_malloc((size_t)length + 1);
    memcpy(result, src + start, (size_t)length);
    result[length] = '\0';
    vm_push(vm, value_string(result));
}

static void builtin_toupper(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toUpperCase");
    char *result = ocl_strdup(args[0].type == VALUE_STRING && args[0].data.string_val
                              ? args[0].data.string_val : "");
    free_args(args, argc);
    for (char *p = result; *p; p++) *p = (char)toupper((unsigned char)*p);
    vm_push(vm, value_string(result));
}

static void builtin_tolower(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toLowerCase");
    char *result = ocl_strdup(args[0].type == VALUE_STRING && args[0].data.string_val
                              ? args[0].data.string_val : "");
    free_args(args, argc);
    for (char *p = result; *p; p++) *p = (char)tolower((unsigned char)*p);
    vm_push(vm, value_string(result));
}

static void builtin_strcontains(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "strContains");
    const char *hay    = (args[0].type == VALUE_STRING) ? args[0].data.string_val : "";
    const char *needle = (args[1].type == VALUE_STRING) ? args[1].data.string_val : "";
    bool found = hay && needle && (strstr(hay, needle) != NULL);
    free_args(args, argc);
    vm_push(vm, value_bool(found));
}

static void builtin_strindexof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "strIndexOf");
    const char *hay    = (args[0].type == VALUE_STRING) ? args[0].data.string_val : "";
    const char *needle = (args[1].type == VALUE_STRING) ? args[1].data.string_val : "";
    int64_t idx = -1;
    if (hay && needle) {
        const char *pos = strstr(hay, needle);
        if (pos) idx = (int64_t)(pos - hay);
    }
    free_args(args, argc);
    vm_push(vm, value_int(idx));
}

static void builtin_strreplace(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 3, "strReplace");

    if (args[0].type != VALUE_STRING) {
        free_args(args, argc);
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    const char *src     = args[0].data.string_val ? args[0].data.string_val : "";
    const char *old_str = (args[1].type == VALUE_STRING) ? args[1].data.string_val : "";
    const char *new_str = (args[2].type == VALUE_STRING) ? args[2].data.string_val : "";

    if (!old_str || old_str[0] == '\0') {
        char *copy = ocl_strdup(src);
        free_args(args, argc);
        vm_push(vm, value_string(copy));
        return;
    }

    size_t old_len = strlen(old_str);
    size_t new_len = new_str ? strlen(new_str) : 0;
    size_t src_len = strlen(src);

    size_t      count = 0;
    const char *cur   = src;
    while ((cur = strstr(cur, old_str)) != NULL) { count++; cur += old_len; }

    size_t  result_len = src_len + count * (new_len > old_len ? new_len - old_len : old_len - new_len) + 1;
    char   *result     = ocl_malloc(result_len);
    char   *dest       = result;
    cur = src;

    const char *found;
    while ((found = strstr(cur, old_str)) != NULL) {
        size_t chunk = (size_t)(found - cur);
        memcpy(dest, cur, chunk);
        dest += chunk;
        if (new_str) { memcpy(dest, new_str, new_len); dest += new_len; }
        cur = found + old_len;
    }
    strcpy(dest, cur);

    free_args(args, argc);
    vm_push(vm, value_string(result));
}

static void builtin_strtrim(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "strTrim");
    const char *s = (args[0].type == VALUE_STRING && args[0].data.string_val)
                    ? args[0].data.string_val : "";
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char *result = ocl_malloc(len + 1);
    memcpy(result, s, len);
    result[len] = '\0';
    free_args(args, argc);
    vm_push(vm, value_string(result));
}

static void builtin_strsplit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "strSplit");
    const char *str   = (args[0].type == VALUE_STRING && args[0].data.string_val)
                        ? args[0].data.string_val : "";
    const char *delim = (args[1].type == VALUE_STRING && args[1].data.string_val)
                        ? args[1].data.string_val : "";
    OclArray *parts = ocl_array_new(8);
    free_args(args, argc);

    if (!delim || delim[0] == '\0') {
        for (size_t i = 0; str[i] != '\0'; i++) {
            char piece[2];
            piece[0] = str[i];
            piece[1] = '\0';
            ocl_array_push(parts, value_string_copy(piece));
        }
        vm_push(vm, value_array(parts));
        ocl_array_release(parts);
        return;
    }

    size_t dlen = strlen(delim);
    const char *cur = str;

    for (;;) {
        const char *found_ptr = strstr(cur, delim);
        size_t part_len = found_ptr ? (size_t)(found_ptr - cur) : strlen(cur);
        char *part = ocl_malloc(part_len + 1);
        memcpy(part, cur, part_len);
        part[part_len] = '\0';
        ocl_array_push(parts, value_string(part));
        if (!found_ptr) break;
        cur = found_ptr + dlen;
    }

    vm_push(vm, value_array(parts));
    ocl_array_release(parts);
}

/* ══════════════════════════════════════════════════════════════════
   Utilities
   ══════════════════════════════════════════════════════════════════ */

static void builtin_exit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int code = (argc >= 1) ? (int)to_int64(args[0]) : 0;
    free_args(args, argc);
    vm->halted    = true;
    vm->exit_code = code;
    vm_push(vm, value_null());
}

static void builtin_to_int(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toInt");
    vm_push(vm, value_int(to_int64(args[0])));
    free_args(args, argc);
}

static void builtin_to_float(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toFloat");
    vm_push(vm, value_float(to_double(args[0])));
    free_args(args, argc);
}

static void builtin_to_string(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toString");
    vm_push(vm, value_string(ocl_strdup(value_to_string(args[0]))));
    free_args(args, argc);
}

static void builtin_to_bool(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toBool");
    vm_push(vm, value_bool(value_is_truthy(args[0])));
    free_args(args, argc);
}

static void builtin_typeof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "typeOf");
    vm_push(vm, value_string(ocl_strdup(value_type_label(args[0]))));
    free_args(args, argc);
}

static void builtin_isnull(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "isNull");
    vm_push(vm, value_bool(args[0].type == VALUE_NULL));
    free_args(args, argc);
}

static void builtin_isint(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "isInt");
    vm_push(vm, value_bool(args[0].type == VALUE_INT));
    free_args(args, argc);
}

static void builtin_isfloat(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "isFloat");
    vm_push(vm, value_bool(args[0].type == VALUE_FLOAT));
    free_args(args, argc);
}

static void builtin_isstring(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "isString");
    vm_push(vm, value_bool(args[0].type == VALUE_STRING));
    free_args(args, argc);
}

static void builtin_isbool(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "isBool");
    vm_push(vm, value_bool(args[0].type == VALUE_BOOL));
    free_args(args, argc);
}

static void builtin_assert(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "assert");
    if (!value_is_truthy(args[0])) {
        if (argc >= 2 && !host_output_quiet()) {
            fputs(value_to_string(args[1]), stderr);
            fputc('\n', stderr);
        } else if (!host_output_quiet()) {
            fputs("assertion failed\n", stderr);
        }
        vm->halted = true;
        vm->exit_code = 1;
    }
    free_args(args, argc);
    vm_push(vm, value_null());
}

static void builtin_timenow(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    free_args(args, argc);
    vm_push(vm, value_int((int64_t)ocl_monotonic_time_ms()));
}

/* ══════════════════════════════════════════════════════════════════
   Array built-ins
   ══════════════════════════════════════════════════════════════════ */

static bool require_int_arg(Value v, const char *name, int position, int64_t *out) {
    if (v.type != VALUE_INT) {
        if (!host_output_quiet())
            fprintf(stderr, "ocl: %s: argument %d must be Int, got %s\n",
                    name, position, value_type_label(v));
        return false;
    }
    if (out) *out = v.data.int_val;
    return true;
}

static bool require_bit_index(Value v, const char *name, int position, int64_t *out) {
    int64_t index = 0;
    if (!require_int_arg(v, name, position, &index))
        return false;
    if (index < 0 || index > 63) {
        if (!host_output_quiet())
            fprintf(stderr, "ocl: %s: argument %d must be between 0 and 63, got %"PRId64"\n",
                    name, position, index);
        return false;
    }
    if (out) *out = index;
    return true;
}

static int64_t popcount_u64(uint64_t value) {
    int64_t count = 0;
    while (value != 0) {
        value &= value - 1;
        count++;
    }
    return count;
}

static int64_t clz_u64(uint64_t value) {
    int64_t count = 0;
    if (value == 0) return 64;
    for (int bit = 63; bit >= 0; bit--) {
        if (((value >> bit) & 1u) != 0) break;
        count++;
    }
    return count;
}

static int64_t ctz_u64(uint64_t value) {
    int64_t count = 0;
    if (value == 0) return 64;
    while ((value & 1u) == 0) {
        value >>= 1;
        count++;
    }
    return count;
}

static void builtin_logical_shift_right(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t count = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitLogicalShiftRight");
    if (!require_int_arg(args[0], "bitLogicalShiftRight", 1, &value) ||
        !require_bit_index(args[1], "bitLogicalShiftRight", 2, &count)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int((int64_t)(((uint64_t)value) >> (uint64_t)count)));
}

static void builtin_rotate_left(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t raw_count = 0;
    uint64_t bits;
    uint64_t count;

    REQUIRE_ARGS(vm, args, argc, 2, "bitRotateLeft");
    if (!require_int_arg(args[0], "bitRotateLeft", 1, &value) ||
        !require_int_arg(args[1], "bitRotateLeft", 2, &raw_count)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    bits = (uint64_t)value;
    count = ((uint64_t)raw_count) & 63u;
    free_args(args, argc);

    if (count == 0) {
        vm_push(vm, value_int(value));
        return;
    }

    vm_push(vm, value_int((int64_t)((bits << count) | (bits >> ((64u - count) & 63u)))));
}

static void builtin_rotate_right(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t raw_count = 0;
    uint64_t bits;
    uint64_t count;

    REQUIRE_ARGS(vm, args, argc, 2, "bitRotateRight");
    if (!require_int_arg(args[0], "bitRotateRight", 1, &value) ||
        !require_int_arg(args[1], "bitRotateRight", 2, &raw_count)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    bits = (uint64_t)value;
    count = ((uint64_t)raw_count) & 63u;
    free_args(args, argc);

    if (count == 0) {
        vm_push(vm, value_int(value));
        return;
    }

    vm_push(vm, value_int((int64_t)((bits >> count) | (bits << ((64u - count) & 63u)))));
}

static void builtin_popcount(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;

    REQUIRE_ARGS(vm, args, argc, 1, "bitPopcount");
    if (!require_int_arg(args[0], "bitPopcount", 1, &value)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int(popcount_u64((uint64_t)value)));
}

static void builtin_count_leading_zeros(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;

    REQUIRE_ARGS(vm, args, argc, 1, "bitCountLeadingZeros");
    if (!require_int_arg(args[0], "bitCountLeadingZeros", 1, &value)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int(clz_u64((uint64_t)value)));
}

static void builtin_count_trailing_zeros(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;

    REQUIRE_ARGS(vm, args, argc, 1, "bitCountTrailingZeros");
    if (!require_int_arg(args[0], "bitCountTrailingZeros", 1, &value)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int(ctz_u64((uint64_t)value)));
}

static void builtin_bit_test(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t index = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitTest");
    if (!require_int_arg(args[0], "bitTest", 1, &value) ||
        !require_bit_index(args[1], "bitTest", 2, &index)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_bool((((uint64_t)value) & (1ull << (uint64_t)index)) != 0));
}

static void builtin_bit_set(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t index = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitSet");
    if (!require_int_arg(args[0], "bitSet", 1, &value) ||
        !require_bit_index(args[1], "bitSet", 2, &index)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int((int64_t)(((uint64_t)value) | (1ull << (uint64_t)index))));
}

static void builtin_bit_clear(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t index = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitClear");
    if (!require_int_arg(args[0], "bitClear", 1, &value) ||
        !require_bit_index(args[1], "bitClear", 2, &index)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int((int64_t)(((uint64_t)value) & ~(1ull << (uint64_t)index))));
}

static void builtin_bit_toggle(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t value = 0;
    int64_t index = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitToggle");
    if (!require_int_arg(args[0], "bitToggle", 1, &value) ||
        !require_bit_index(args[1], "bitToggle", 2, &index)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int((int64_t)(((uint64_t)value) ^ (1ull << (uint64_t)index))));
}

static void builtin_bit_nand(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t a = 0;
    int64_t b = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitNand");
    if (!require_int_arg(args[0], "bitNand", 1, &a) ||
        !require_int_arg(args[1], "bitNand", 2, &b)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int(~(a & b)));
}

static void builtin_bit_nor(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t a = 0;
    int64_t b = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitNor");
    if (!require_int_arg(args[0], "bitNor", 1, &a) ||
        !require_int_arg(args[1], "bitNor", 2, &b)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int(~(a | b)));
}

static void builtin_bit_xnor(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t a = 0;
    int64_t b = 0;

    REQUIRE_ARGS(vm, args, argc, 2, "bitXnor");
    if (!require_int_arg(args[0], "bitXnor", 1, &a) ||
        !require_int_arg(args[1], "bitXnor", 2, &b)) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }

    free_args(args, argc);
    vm_push(vm, value_int(~(a ^ b)));
}

static void builtin_array_new(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t sz  = (argc >= 1) ? to_int64(args[0]) : 0;
    free_args(args, argc);

    if (sz < 0) sz = 0;
    OclArray *arr = ocl_array_new((size_t)sz);
    for (int64_t i = 0; i < sz; i++)
        ocl_array_push(arr, value_null());
    vm_push(vm, value_array(arr));
    ocl_array_release(arr);
}

static void builtin_array_push(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "arrayPush");

    if (args[0].type != VALUE_ARRAY) {
        if (!host_output_quiet())
            fprintf(stderr, "ocl: arrayPush: first argument must be Array\n");
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    ocl_array_push(args[0].data.array_val, args[1]);
    free_args(args, argc);
    vm_push(vm, value_null());
}

static void builtin_array_pop(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "arrayPop");

    if (args[0].type != VALUE_ARRAY || !args[0].data.array_val ||
        args[0].data.array_val->length == 0) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    OclArray *arr  = args[0].data.array_val;
    size_t    last = arr->length - 1;
    Value     v    = value_own_copy(arr->elements[last]);
    value_free(arr->elements[last]);
    arr->elements[last] = value_null();
    arr->length--;
    free_args(args, argc);
    vm_push(vm, v);
}

static void builtin_array_get(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "arrayGet");

    if (args[0].type != VALUE_ARRAY) {
        if (!host_output_quiet())
            fprintf(stderr, "ocl: arrayGet: first argument must be Array\n");
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    int64_t idx = to_int64(args[1]);
    Value   v   = (idx >= 0 && args[0].data.array_val &&
                   (size_t)idx < args[0].data.array_val->length)
                  ? ocl_array_get(args[0].data.array_val, (size_t)idx)
                  : value_null();
    free_args(args, argc);
    vm_push(vm, v);
}

static void builtin_array_set(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 3, "arraySet");

    if (args[0].type != VALUE_ARRAY) {
        if (!host_output_quiet())
            fprintf(stderr, "ocl: arraySet: first argument must be Array\n");
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    int64_t idx = to_int64(args[1]);
    if (idx >= 0 && args[0].data.array_val)
        ocl_array_set(args[0].data.array_val, (size_t)idx, args[2]);
    else if (idx < 0 && !host_output_quiet())
        fprintf(stderr, "ocl: arraySet: negative index %"PRId64"\n", idx);
    free_args(args, argc);
    vm_push(vm, value_null());
}

static void builtin_array_len(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "arrayLen");
    int64_t len = (args[0].type == VALUE_ARRAY && args[0].data.array_val)
                  ? (int64_t)args[0].data.array_val->length : 0;
    free_args(args, argc);
    vm_push(vm, value_int(len));
}

/* ══════════════════════════════════════════════════════════════════
   Random
   ══════════════════════════════════════════════════════════════════ */

static void builtin_listfiles(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "listFiles");

    const char *root_src = (args[0].type == VALUE_STRING && args[0].data.string_val)
                         ? args[0].data.string_val
                         : value_to_string(args[0]);
    char *root = ocl_strdup(root_src ? root_src : "");
    free_args(args, argc);

    OclArray *files = ocl_array_new(16);
    host_list_files_recursive(root, files);
    ocl_free(root);

    vm_push(vm, value_array(files));
    ocl_array_release(files);
}

static void builtin_measurefile(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "measureFile");

    const char *path_src = (args[0].type == VALUE_STRING && args[0].data.string_val)
                         ? args[0].data.string_val
                         : value_to_string(args[0]);
    char *path = ocl_strdup(path_src ? path_src : "");
    free_args(args, argc);

    double elapsed_ms = 0.0;
    if (host_measure_source_file(path, &elapsed_ms)) {
        vm_push(vm, value_float(elapsed_ms));
    } else {
        vm_push(vm, value_null());
    }
    ocl_free(path);
}

static uint32_t ocl_rand32(void) {
    uint32_t value = 0;

#if defined(_WIN32)
    if (BCryptGenRandom(NULL, (PUCHAR)&value, (ULONG)sizeof(value),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        return value;
    }
#elif defined(__APPLE__)
    arc4random_buf(&value, sizeof(value));
    return value;
#elif defined(__linux__)
    ssize_t got = getrandom(&value, sizeof(value), 0);
    if (got == (ssize_t)sizeof(value))
        return value;
#endif

    value = (uint32_t)time(NULL) ^ (uint32_t)ocl_monotonic_time_ms();
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void builtin_random(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc >= 2) {
        int64_t  lo    = to_int64(args[0]);
        int64_t  hi    = to_int64(args[1]);
        free_args(args, argc);
        if (hi <= lo) { vm_push(vm, value_int(lo)); return; }
        uint64_t range = (uint64_t)(hi - lo) + 1;
        vm_push(vm, value_int(lo + (int64_t)(ocl_rand32() % range)));
    } else if (argc == 1) {
        int64_t n = to_int64(args[0]);
        free_args(args, argc);
        if (n <= 0) { vm_push(vm, value_int(0)); return; }
        vm_push(vm, value_int((int64_t)(ocl_rand32() % (uint64_t)n)));
    } else {
        free_args(args, argc);
        vm_push(vm, value_float((double)ocl_rand32() / ((double)UINT32_MAX + 1.0)));
    }
}

/* ══════════════════════════════════════════════════════════════════
   Dispatch table
   ══════════════════════════════════════════════════════════════════ */

static const StdlibEntry STDLIB_TABLE[] = {
    { BUILTIN_PRINT,       "print",       builtin_print,       OCL_VARIADIC_ARGS(1), TYPE_VOID    },
    { BUILTIN_PRINTF,      "printf",      builtin_printf,      OCL_VARIADIC_ARGS(1), TYPE_VOID    },
    { BUILTIN_INPUT,       "input",       builtin_input,       OCL_VARIADIC_ARGS(0), TYPE_STRING  },
    { BUILTIN_READLINE,    "readLine",    builtin_readline,    0,                    TYPE_STRING  },
    { BUILTIN_ABS,         "abs",         builtin_abs,         1,                    TYPE_UNKNOWN },
    { BUILTIN_SQRT,        "sqrt",        builtin_sqrt,        1,                    TYPE_FLOAT   },
    { BUILTIN_POW,         "pow",         builtin_pow,         2,                    TYPE_FLOAT   },
    { BUILTIN_SIN,         "sin",         builtin_sin,         1,                    TYPE_FLOAT   },
    { BUILTIN_COS,         "cos",         builtin_cos,         1,                    TYPE_FLOAT   },
    { BUILTIN_TAN,         "tan",         builtin_tan,         1,                    TYPE_FLOAT   },
    { BUILTIN_FLOOR,       "floor",       builtin_floor_fn,    1,                    TYPE_FLOAT   },
    { BUILTIN_CEIL,        "ceil",        builtin_ceil_fn,     1,                    TYPE_FLOAT   },
    { BUILTIN_ROUND,       "round",       builtin_round_fn,    1,                    TYPE_FLOAT   },
    { BUILTIN_MAX,         "max",         builtin_max,         2,                    TYPE_UNKNOWN },
    { BUILTIN_MIN,         "min",         builtin_min,         2,                    TYPE_UNKNOWN },
    { BUILTIN_STRLEN,      "strLen",      builtin_strlen,      1,                    TYPE_INT     },
    { BUILTIN_SUBSTR,      "substr",      builtin_substr,      OCL_VARIADIC_ARGS(2), TYPE_STRING  },
    { BUILTIN_TOUPPER,     "toUpperCase", builtin_toupper,     1,                    TYPE_STRING  },
    { BUILTIN_TOLOWER,     "toLowerCase", builtin_tolower,     1,                    TYPE_STRING  },
    { BUILTIN_STRCONTAINS, "strContains", builtin_strcontains, 2,                    TYPE_BOOL    },
    { BUILTIN_STRINDEXOF,  "strIndexOf",  builtin_strindexof,  2,                    TYPE_INT     },
    { BUILTIN_STRREPLACE,  "strReplace",  builtin_strreplace,  3,                    TYPE_STRING  },
    { BUILTIN_STRTRIM,     "strTrim",     builtin_strtrim,     1,                    TYPE_STRING  },
    { BUILTIN_STRSPLIT,    "strSplit",    builtin_strsplit,    2,                    TYPE_ARRAY   },
    { BUILTIN_TOINT,       "toInt",       builtin_to_int,      1,                    TYPE_INT     },
    { BUILTIN_TOFLOAT,     "toFloat",     builtin_to_float,    1,                    TYPE_FLOAT   },
    { BUILTIN_TOSTRING,    "toString",    builtin_to_string,   1,                    TYPE_STRING  },
    { BUILTIN_STRFORMAT,   "strFormat",   builtin_strformat,   OCL_VARIADIC_ARGS(1), TYPE_STRING  },
    { BUILTIN_TOBOOL,      "toBool",      builtin_to_bool,     1,                    TYPE_BOOL    },
    { BUILTIN_TYPEOF,      "typeOf",      builtin_typeof,      1,                    TYPE_STRING  },
    { BUILTIN_ISNULL,      "isNull",      builtin_isnull,      1,                    TYPE_BOOL    },
    { BUILTIN_ISINT,       "isInt",       builtin_isint,       1,                    TYPE_BOOL    },
    { BUILTIN_ISFLOAT,     "isFloat",     builtin_isfloat,     1,                    TYPE_BOOL    },
    { BUILTIN_ISSTRING,    "isString",    builtin_isstring,    1,                    TYPE_BOOL    },
    { BUILTIN_ISBOOL,      "isBool",      builtin_isbool,      1,                    TYPE_BOOL    },
    { BUILTIN_EXIT,        "exit",        builtin_exit,        OCL_VARIADIC_ARGS(0), TYPE_VOID    },
    { BUILTIN_ASSERT,      "assert",      builtin_assert,      OCL_VARIADIC_ARGS(1), TYPE_VOID    },
    { BUILTIN_TIMENOW,     "timeNow",     builtin_timenow,     0,                    TYPE_INT     },
    { BUILTIN_LOGICAL_SHIFT_RIGHT, "bitLogicalShiftRight", builtin_logical_shift_right, 2, TYPE_INT },
    { BUILTIN_ROTATE_LEFT, "bitRotateLeft",  builtin_rotate_left, 2,                  TYPE_INT     },
    { BUILTIN_ROTATE_RIGHT,"bitRotateRight", builtin_rotate_right,2,                  TYPE_INT     },
    { BUILTIN_POPCOUNT,    "bitPopcount",   builtin_popcount,    1,                  TYPE_INT     },
    { BUILTIN_CLZ,         "bitCountLeadingZeros",  builtin_count_leading_zeros,  1, TYPE_INT     },
    { BUILTIN_CTZ,         "bitCountTrailingZeros", builtin_count_trailing_zeros, 1, TYPE_INT     },
    { BUILTIN_BIT_TEST,    "bitTest",     builtin_bit_test,    2,                    TYPE_BOOL    },
    { BUILTIN_BIT_SET,     "bitSet",      builtin_bit_set,     2,                    TYPE_INT     },
    { BUILTIN_BIT_CLEAR,   "bitClear",    builtin_bit_clear,   2,                    TYPE_INT     },
    { BUILTIN_BIT_TOGGLE,  "bitToggle",   builtin_bit_toggle,  2,                    TYPE_INT     },
    { BUILTIN_BIT_NAND,    "bitNand",     builtin_bit_nand,    2,                    TYPE_INT     },
    { BUILTIN_BIT_NOR,     "bitNor",      builtin_bit_nor,     2,                    TYPE_INT     },
    { BUILTIN_BIT_XNOR,    "bitXnor",     builtin_bit_xnor,    2,                    TYPE_INT     },
    { BUILTIN_ARRAY_NEW,   "arrayNew",    builtin_array_new,   OCL_VARIADIC_ARGS(0), TYPE_ARRAY   },
    { BUILTIN_ARRAY_PUSH,  "arrayPush",   builtin_array_push,  2,                    TYPE_VOID    },
    { BUILTIN_ARRAY_POP,   "arrayPop",    builtin_array_pop,   1,                    TYPE_UNKNOWN },
    { BUILTIN_ARRAY_GET,   "arrayGet",    builtin_array_get,   2,                    TYPE_UNKNOWN },
    { BUILTIN_ARRAY_SET,   "arraySet",    builtin_array_set,   3,                    TYPE_VOID    },
    { BUILTIN_ARRAY_LEN,   "arrayLen",    builtin_array_len,   1,                    TYPE_INT     },
    { BUILTIN_RANDOM,      "random",      builtin_random,      OCL_VARIADIC_ARGS(0), TYPE_UNKNOWN },
    { BUILTIN_LISTFILES,   "listFiles",   builtin_listfiles,   1,                    TYPE_ARRAY   },
    { BUILTIN_MEASUREFILE, "measureFile", builtin_measurefile, 1,                    TYPE_FLOAT   },
    { BUILTIN_TERMINAL_ARGS,          "__terminalArgs",         builtin_terminal_args,          0, TYPE_ARRAY  },
    { BUILTIN_TERMINAL_EXEC,          "__terminalExec",         builtin_terminal_exec,          2, TYPE_INT    },
    { BUILTIN_TERMINAL_CAPTURE,       "__terminalCapture",      builtin_terminal_capture,       2, TYPE_ARRAY  },
    { BUILTIN_TERMINAL_SHELL,         "__terminalShell",        builtin_terminal_shell,         1, TYPE_INT    },
    { BUILTIN_TERMINAL_SHELL_CAPTURE, "__terminalShellCapture", builtin_terminal_shell_capture, 1, TYPE_ARRAY  },
    { BUILTIN_TERMINAL_OS,            "__terminalOs",           builtin_terminal_os,            0, TYPE_STRING },
};

static const size_t STDLIB_TABLE_SIZE =
    sizeof(STDLIB_TABLE) / sizeof(STDLIB_TABLE[0]);

/* ── Public API ───────────────────────────────────────────────────── */

bool stdlib_dispatch(VM *vm, int id, int argc) {
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++) {
        if (STDLIB_TABLE[i].id == id) {
            STDLIB_TABLE[i].fn(vm, argc);
            return true;
        }
    }
    return false;
}

const StdlibEntry *stdlib_lookup_by_name(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++)
        if (strcmp(STDLIB_TABLE[i].name, name) == 0)
            return &STDLIB_TABLE[i];
    return NULL;
}

const StdlibEntry *stdlib_get_table(size_t *out_count) {
    if (out_count) *out_count = STDLIB_TABLE_SIZE;
    return STDLIB_TABLE;
}
