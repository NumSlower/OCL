#include "elf_emitter.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "common.h"

#if defined(__linux__)
#include <ctype.h>

static bool build_path(char *out, size_t out_size, const char *fmt, ...);

static bool copy_string(char *out, size_t out_size, const char *value) {
    int n;

    if (!out || out_size == 0 || !value) return false;
    n = snprintf(out, out_size, "%s", value);
    return n >= 0 && (size_t)n < out_size;
}

static bool dirname_in_place(char *path) {
    char *slash;

    if (!path || !*path) return false;
    slash = strrchr(path, '/');
    if (!slash) return false;
    if (slash == path) {
        path[1] = '\0';
        return true;
    }

    *slash = '\0';
    return true;
}

static bool resolve_project_root(char *out, size_t out_size, ErrorCollector *errors) {
    const char *env_root = getenv("OCL_PROJECT_ROOT");
    char exe_path[PATH_MAX];
    ssize_t len;

    if (env_root && *env_root)
        return copy_string(out, out_size, env_root);

    len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to resolve OCL project root: %s", strerror(errno));
        return false;
    }
    exe_path[len] = '\0';

    if (!dirname_in_place(exe_path)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to resolve OCL project root from executable path");
        return false;
    }

    if (!copy_string(out, out_size, exe_path)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "project root path is too long");
        return false;
    }

    return true;
}

static bool resolve_user_root(char *out, size_t out_size,
                              const char *project_root, ErrorCollector *errors) {
    const char *env_root = getenv("OCL_NUMOS_USER_ROOT");

    if (env_root && *env_root)
        return copy_string(out, out_size, env_root);

    if (!build_path(out, out_size, "%s/../..", project_root)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "NumOS user root path is too long");
        return false;
    }

    return true;
}

static bool resolve_numos_port_root(char *out, size_t out_size,
                                    const char *user_root, ErrorCollector *errors) {
    const char *env_root = getenv("OCL_NUMOS_PORT_ROOT");

    if (env_root && *env_root)
        return copy_string(out, out_size, env_root);

    if (!build_path(out, out_size, "%s/ports/ocl", user_root)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "NumOS port root path is too long");
        return false;
    }

    return true;
}

static const char *resolve_tool(const char *env_name, const char *fallback) {
    const char *value = getenv(env_name);

    if (value && *value) return value;
    return fallback;
}

static void emit_c_string(FILE *out, const char *s) {
    fputc('"', out);
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
            switch (*p) {
                case '\\': fputs("\\\\", out); break;
                case '"':  fputs("\\\"", out); break;
                case '\n': fputs("\\n", out); break;
                case '\r': fputs("\\r", out); break;
                case '\t': fputs("\\t", out); break;
                default:
                    if (isprint(*p)) fputc((int)*p, out);
                    else fprintf(out, "\\%03o", *p);
                    break;
            }
        }
    }
    fputc('"', out);
}

static void emit_c_string_or_null(FILE *out, const char *s) {
    if (!s) {
        fputs("NULL", out);
        return;
    }
    emit_c_string(out, s);
}

static bool write_generated_source(FILE *out, Bytecode *bytecode, ErrorCollector *errors) {
    static const char *opcode_names[] = {
        "OP_PUSH_CONST", "OP_POP",
        "OP_LOAD_VAR", "OP_STORE_VAR", "OP_LOAD_GLOBAL", "OP_STORE_GLOBAL",
        "OP_ADD", "OP_SUBTRACT", "OP_MULTIPLY", "OP_DIVIDE", "OP_MODULO", "OP_NEGATE", "OP_NOT",
        "OP_EQUAL", "OP_NOT_EQUAL", "OP_LESS", "OP_LESS_EQUAL", "OP_GREATER", "OP_GREATER_EQUAL",
        "OP_AND", "OP_OR",
        "OP_JUMP", "OP_JUMP_IF_FALSE", "OP_JUMP_IF_TRUE",
        "OP_CALL", "OP_RETURN", "OP_HALT",
        "OP_ARRAY_NEW", "OP_ARRAY_GET", "OP_ARRAY_SET", "OP_ARRAY_LEN",
        "OP_BIT_AND", "OP_BIT_OR", "OP_BIT_XOR", "OP_BIT_NOT", "OP_LSHIFT", "OP_RSHIFT",
        "OP_STRUCT_NEW", "OP_STRUCT_GET", "OP_STRUCT_SET",
    };

    if (!out || !bytecode) return false;

    fputs(
        "#define _POSIX_C_SOURCE 200809L\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include \"common.h\"\n"
        "#include \"bytecode.h\"\n"
        "#include \"errors.h\"\n"
        "#include \"vm.h\"\n"
        "\n"
        "static Bytecode *build_bytecode(void) {\n"
        "    Bytecode *bc = bytecode_create();\n"
        "\n",
        out);

    for (size_t i = 0; i < bytecode->constant_count; i++) {
        Value v = bytecode->constants[i];
        fputs("    (void)bytecode_add_constant(bc, ", out);
        switch (v.type) {
            case VALUE_INT:
                fprintf(out, "value_int(%" PRId64 ")", v.data.int_val);
                break;
            case VALUE_FLOAT:
                fprintf(out, "value_float(%.17g)", v.data.float_val);
                break;
            case VALUE_STRING:
                fputs("value_string_copy(", out);
                emit_c_string_or_null(out, v.data.string_val);
                fputs(")", out);
                break;
            case VALUE_BOOL:
                fprintf(out, "value_bool(%s)", v.data.bool_val ? "true" : "false");
                break;
            case VALUE_CHAR:
                fprintf(out, "value_char(%u)", (unsigned int)(unsigned char)v.data.char_val);
                break;
            case VALUE_NULL:
                fputs("value_null()", out);
                break;
            case VALUE_ARRAY:
            case VALUE_STRUCT:
                error_add(errors, ERRK_LOGIC, ERROR_CODEGEN, LOC_NONE,
                          "ELF emitter does not support aggregate constants");
                return false;
        }
        fputs(");\n", out);
    }

    if (bytecode->constant_count > 0) fputc('\n', out);

    for (size_t i = 0; i < bytecode->function_count; i++) {
        FuncEntry *fe = &bytecode->functions[i];
        if (!fe->name) {
            error_add(errors, ERRK_LOGIC, ERROR_CODEGEN, LOC_NONE,
                      "ELF emitter saw a function without a name");
            return false;
        }
        fprintf(out,
                "    uint32_t fn_%zu = bytecode_add_function(bc, ",
                i);
        emit_c_string(out, fe->name);
        fprintf(out, ", %u, %d);\n", fe->start_ip, fe->param_count);
        fprintf(out, "    bc->functions[fn_%zu].local_count = %d;\n", i, fe->local_count);
    }

    if (bytecode->function_count > 0) fputc('\n', out);

    for (size_t i = 0; i < bytecode->instruction_count; i++) {
        Instruction *ins = &bytecode->instructions[i];
        if ((size_t)ins->opcode >= (sizeof(opcode_names) / sizeof(opcode_names[0]))) {
            error_add(errors, ERRK_LOGIC, ERROR_CODEGEN, LOC_NONE,
                      "ELF emitter saw unknown opcode %u", (unsigned)ins->opcode);
            return false;
        }

        fprintf(out, "    bytecode_emit(bc, %s, %u, %u, (SourceLocation){%d, %d, ",
                opcode_names[ins->opcode], ins->operand1, ins->operand2,
                ins->location.line, ins->location.column);
        emit_c_string_or_null(out, ins->location.filename);
        fputs("});\n", out);
    }

    fputs(
        "\n"
        "    return bc;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    int exit_code = 0;\n"
        "    ErrorCollector *errors = error_collector_create();\n"
        "    Bytecode *bytecode = build_bytecode();\n"
        "    VM *vm = vm_create(bytecode, errors);\n"
        "\n"
        "    if (!vm) {\n"
        "        error_add(errors, ERRK_LOGIC, ERROR_RUNTIME, LOC_NONE,\n"
        "                  \"failed to create VM\");\n"
        "        error_print_all(errors);\n"
        "        error_collector_free(errors);\n"
        "        bytecode_free(bytecode);\n"
        "        return 1;\n"
        "    }\n"
        "\n"
        "    exit_code = vm_execute(vm);\n"
        "    if (error_has_errors(errors)) {\n"
        "        error_print_all(errors);\n"
        "        error_collector_reset(errors);\n"
        "    } else if (vm->has_result && vm->result.type != VALUE_NULL) {\n"
        "        puts(value_to_string(vm->result));\n"
        "    }\n"
        "\n"
        "    vm_free(vm);\n"
        "    bytecode_free(bytecode);\n"
        "    error_collector_free(errors);\n"
        "    return exit_code;\n"
        "}\n",
        out);

    if (ferror(out)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to write generated ELF launcher");
        return false;
    }

    return true;
}
#endif

#if defined(__linux__)
static bool build_path(char *out, size_t out_size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, out_size, fmt, ap);
    va_end(ap);
    return n >= 0 && (size_t)n < out_size;
}

static bool run_tool(const char *tool, char *const argv[],
                     const char *phase, ErrorCollector *errors) {
    pid_t pid = fork();
    if (pid < 0) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to start %s: %s", phase, strerror(errno));
        return false;
    }

    if (pid == 0) {
        execvp(tool, argv);
        fprintf(stderr, "ocl: failed to execute '%s': %s\n", tool, strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed while waiting for %s: %s", phase, strerror(errno));
        return false;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return true;

    if (WIFEXITED(status)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "%s exited with status %d", phase, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "%s terminated by signal %d", phase, WTERMSIG(status));
    } else {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "%s failed", phase);
    }

    return false;
}

static bool run_compiler(const char *source_path, const char *output_path,
                         ErrorCollector *errors) {
    const char *cc;
    const char *ld;
    const char *as;
    char project_root[PATH_MAX];
    char user_root[PATH_MAX];
    char port_root[PATH_MAX];
    char user_include_dir[PATH_MAX];
    char include_dir[PATH_MAX];
    char linker_path[PATH_MAX];
    char entry_asm[PATH_MAX];
    char user_runtime[PATH_MAX];
    char user_libc[PATH_MAX];
    char common_src[PATH_MAX];
    char errors_src[PATH_MAX];
    char compat_src[PATH_MAX];
    char stdlib_src[PATH_MAX];
    char bytecode_src[PATH_MAX];
    char vm_runtime_src[PATH_MAX];
    char vm_src[PATH_MAX];

    cc = resolve_tool(
        "OCL_NUMOS_CC",
#ifdef OCL_DEFAULT_NUMOS_CC
        OCL_DEFAULT_NUMOS_CC
#else
        NULL
#endif
    );
    ld = resolve_tool(
        "OCL_NUMOS_LD",
#ifdef OCL_DEFAULT_NUMOS_LD
        OCL_DEFAULT_NUMOS_LD
#else
        NULL
#endif
    );
    as = resolve_tool(
        "OCL_NUMOS_AS",
#ifdef OCL_DEFAULT_NUMOS_AS
        OCL_DEFAULT_NUMOS_AS
#else
        NULL
#endif
    );

    if (!cc || !*cc || !ld || !*ld || !as || !*as) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "missing NumOS toolchain configuration, set OCL_NUMOS_CC, OCL_NUMOS_LD, and OCL_NUMOS_AS");
        return false;
    }

    if (!resolve_project_root(project_root, sizeof(project_root), errors) ||
        !resolve_user_root(user_root, sizeof(user_root), project_root, errors) ||
        !resolve_numos_port_root(port_root, sizeof(port_root), user_root, errors) ||
        !build_path(user_include_dir, sizeof(user_include_dir), "%s/include", user_root) ||
        !build_path(include_dir, sizeof(include_dir), "%s/include", project_root) ||
        !build_path(linker_path, sizeof(linker_path), "%s/linker/linker.ld", user_root) ||
        !build_path(entry_asm, sizeof(entry_asm), "%s/runtime/entry.asm", user_root) ||
        !build_path(user_runtime, sizeof(user_runtime), "%s/runtime/runtime.c", user_root) ||
        !build_path(user_libc, sizeof(user_libc), "%s/runtime/libc.c", user_root) ||
        !build_path(common_src, sizeof(common_src), "%s/src/common.c", project_root) ||
        !build_path(errors_src, sizeof(errors_src), "%s/src/frontend/errors.c", project_root) ||
        !build_path(compat_src, sizeof(compat_src), "%s/numos_compat.c", port_root) ||
        !build_path(stdlib_src, sizeof(stdlib_src), "%s/src/stdlib/stdlib.c", project_root) ||
        !build_path(bytecode_src, sizeof(bytecode_src), "%s/src/vm/bytecode.c", project_root) ||
        !build_path(vm_runtime_src, sizeof(vm_runtime_src), "%s/src/vm/runtime.c", project_root) ||
        !build_path(vm_src, sizeof(vm_src), "%s/src/vm/vm.c", project_root)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "project paths are too long for NumOS ELF emission");
        return false;
    }

    char include_user_flag[PATH_MAX + 2];
    char include_ocl_flag[PATH_MAX + 2];
    if (!build_path(include_user_flag, sizeof(include_user_flag), "-I%s", user_include_dir) ||
        !build_path(include_ocl_flag, sizeof(include_ocl_flag), "-I%s", include_dir)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "include path is too long for NumOS ELF emission");
        return false;
    }

    char tmp_dir[PATH_MAX];
    size_t source_len = strlen(source_path);
    if (source_len >= sizeof(tmp_dir)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "temporary source path is too long");
        return false;
    }
    memcpy(tmp_dir, source_path, source_len + 1);
    char *slash = strrchr(tmp_dir, '/');
    if (!slash) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "temporary source path is invalid");
        return false;
    }
    *slash = '\0';

    char entry_obj[PATH_MAX];
    char runner_obj[PATH_MAX];
    char runtime_obj[PATH_MAX];
    char libc_obj[PATH_MAX];
    char common_obj[PATH_MAX];
    char errors_obj[PATH_MAX];
    char compat_obj[PATH_MAX];
    char stdlib_obj[PATH_MAX];
    char bytecode_obj[PATH_MAX];
    char vm_runtime_obj[PATH_MAX];
    char vm_obj[PATH_MAX];

    if (!build_path(entry_obj, sizeof(entry_obj), "%s/enteryusr.o", tmp_dir) ||
        !build_path(runner_obj, sizeof(runner_obj), "%s/runner.o", tmp_dir) ||
        !build_path(runtime_obj, sizeof(runtime_obj), "%s/runtime.o", tmp_dir) ||
        !build_path(libc_obj, sizeof(libc_obj), "%s/libc.o", tmp_dir) ||
        !build_path(common_obj, sizeof(common_obj), "%s/common.o", tmp_dir) ||
        !build_path(errors_obj, sizeof(errors_obj), "%s/errors.o", tmp_dir) ||
        !build_path(compat_obj, sizeof(compat_obj), "%s/numos_compat.o", tmp_dir) ||
        !build_path(stdlib_obj, sizeof(stdlib_obj), "%s/stdlib.o", tmp_dir) ||
        !build_path(bytecode_obj, sizeof(bytecode_obj), "%s/bytecode.o", tmp_dir) ||
        !build_path(vm_runtime_obj, sizeof(vm_runtime_obj), "%s/vm_runtime.o", tmp_dir) ||
        !build_path(vm_obj, sizeof(vm_obj), "%s/vm.o", tmp_dir)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "temporary object path is too long");
        return false;
    }

    char *const asm_argv[] = {
        (char *)as,
        "-f", "elf64",
        entry_asm,
        "-o", entry_obj,
        NULL
    };
    if (!run_tool(as, asm_argv, "NumOS assembler", errors))
        return false;

    const char *cflags[] = {
        "-m64",
        "-ffreestanding",
        "-fstack-protector-strong",
        "-mstack-protector-guard=global",
        "-fno-pic",
        "-fno-builtin",
        "-nostdlib",
        "-nostartfiles",
        "-nodefaultlibs",
        "-fno-tree-vectorize",
        "-mno-mmx",
        "-msse",
        "-msse2",
        "-mfpmath=sse",
        "-Wall",
        "-Wextra",
        "-O2",
        NULL
    };

#define RUN_CC(src, obj, phase_name) do { \
        char *argv_local[32]; \
        int argc_local = 0; \
        argv_local[argc_local++] = (char *)cc; \
        for (int i_local = 0; cflags[i_local]; i_local++) \
            argv_local[argc_local++] = (char *)cflags[i_local]; \
        argv_local[argc_local++] = include_user_flag; \
        argv_local[argc_local++] = include_ocl_flag; \
        argv_local[argc_local++] = "-c"; \
        argv_local[argc_local++] = (char *)(src); \
        argv_local[argc_local++] = "-o"; \
        argv_local[argc_local++] = (char *)(obj); \
        argv_local[argc_local] = NULL; \
        if (!run_tool(cc, argv_local, (phase_name), errors)) \
            return false; \
    } while (0)

    RUN_CC(source_path,    runner_obj,     "NumOS C compiler");
    RUN_CC(user_runtime,   runtime_obj,    "NumOS C compiler");
    RUN_CC(user_libc,      libc_obj,       "NumOS C compiler");
    RUN_CC(common_src,     common_obj,     "NumOS C compiler");
    RUN_CC(errors_src,     errors_obj,     "NumOS C compiler");
    RUN_CC(compat_src,     compat_obj,     "NumOS C compiler");
    RUN_CC(stdlib_src,     stdlib_obj,     "NumOS C compiler");
    RUN_CC(bytecode_src,   bytecode_obj,   "NumOS C compiler");
    RUN_CC(vm_runtime_src, vm_runtime_obj, "NumOS C compiler");
    RUN_CC(vm_src,         vm_obj,         "NumOS C compiler");

#undef RUN_CC

    char *const link_argv[] = {
        (char *)ld,
        "-m", "elf_x86_64",
        "-nostdlib",
        "-static",
        "-T", linker_path,
        "--entry=_start",
        "--build-id=none",
        "-z", "noexecstack",
        "-o", (char *)output_path,
        entry_obj,
        runtime_obj,
        libc_obj,
        runner_obj,
        common_obj,
        errors_obj,
        compat_obj,
        stdlib_obj,
        bytecode_obj,
        vm_runtime_obj,
        vm_obj,
        NULL
    };

    if (!run_tool(ld, link_argv, "NumOS linker", errors))
        return false;

    return true;
}
#endif

bool elf_emit_binary(Bytecode *bytecode, const char *output_path,
                     ErrorCollector *errors) {
#if !defined(__linux__)
    (void)bytecode;
    (void)output_path;
    error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
              "NumOS ELF output is only supported on Linux hosts");
    return false;
#else
    if (!bytecode || !output_path || !*output_path) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "missing bytecode or output path for ELF emission");
        return false;
    }

    char tmp_dir_template[] = "/tmp/ocl-elf-XXXXXX";
    char *tmp_dir = mkdtemp(tmp_dir_template);
    if (!tmp_dir) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to create temporary build directory: %s", strerror(errno));
        return false;
    }

    char source_path[PATH_MAX];
    if (snprintf(source_path, sizeof(source_path), "%s/runner.c", tmp_dir) >= (int)sizeof(source_path)) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "temporary source path is too long");
        rmdir(tmp_dir);
        return false;
    }

    FILE *out = fopen(source_path, "wb");
    if (!out) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to open temporary source file '%s': %s",
                  source_path, strerror(errno));
        rmdir(tmp_dir);
        return false;
    }

    bool ok = write_generated_source(out, bytecode, errors);
    if (fclose(out) != 0 && ok) {
        error_add(errors, ERRK_OPERATION, ERROR_CODEGEN, LOC_NONE,
                  "failed to close temporary source file '%s': %s",
                  source_path, strerror(errno));
        ok = false;
    }

    if (ok)
        ok = run_compiler(source_path, output_path, errors);

    remove(source_path);
    rmdir(tmp_dir);
    return ok;
#endif
}
