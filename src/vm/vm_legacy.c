#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "vm.h"
#include "common.h"
#include "errors.h"
#include "ocl_stdlib.h"
#include "runtime.h"

static void vm_error_kind(VM *vm, ErrorKind kind, SourceLocation loc,
                          const char *fmt, ...) OCL_PRINTF_LIKE(4, 5);
static void vm_store_result(VM *vm, Value value);
static void vm_clear_program_args(VM *vm);
static void vm_release_frame(CallFrame *frame);

static bool vm_accepts_argc(int param_count, uint32_t argc) {
    if (OCL_ARGS_VARIADIC(param_count))
        return (int)argc >= OCL_ARGS_MIN(param_count);
    return param_count == (int)argc;
}

/* ══════════════════════════════════════════════════════════════════
   Lifecycle
   ══════════════════════════════════════════════════════════════════ */

VM *vm_create(Bytecode *bytecode, ErrorCollector *errors) {
    if (!bytecode) {
        fprintf(stderr, "ocl: vm_create: NULL bytecode\n");
        return NULL;
    }
    VM *vm = ocl_malloc(sizeof(VM));
    *vm = (VM){0};

    vm->bytecode       = bytecode;
    vm->errors         = errors;
    vm->frame_capacity = VM_FRAMES_INITIAL;
    vm->frames         = ocl_malloc(vm->frame_capacity * sizeof(CallFrame));
    vm->result         = value_null();
    return vm;
}

static void vm_clear_program_args(VM *vm) {
    if (!vm || !vm->program_args) {
        if (vm) vm->program_argc = 0;
        return;
    }

    for (int i = 0; i < vm->program_argc; i++)
        ocl_free(vm->program_args[i]);
    ocl_free(vm->program_args);
    vm->program_args = NULL;
    vm->program_argc = 0;
}

static void vm_release_frame(CallFrame *frame) {
    if (!frame) return;

    for (size_t i = 0; i < frame->local_capacity; i++)
        ocl_cell_release(frame->locals[i]);
    ocl_free(frame->locals);
    ocl_function_release(frame->closure);
    frame->locals = NULL;
    frame->local_count = 0;
    frame->local_capacity = 0;
    frame->closure = NULL;
}

void vm_set_program_args(VM *vm, int argc, const char *const *argv) {
    if (!vm) return;

    vm_clear_program_args(vm);
    if (argc <= 0 || !argv)
        return;

    vm->program_args = ocl_malloc((size_t)argc * sizeof(char *));
    vm->program_argc = argc;
    for (int i = 0; i < argc; i++)
        vm->program_args[i] = ocl_strdup(argv[i] ? argv[i] : "");
}

void vm_free(VM *vm) {
    if (!vm) return;

    for (size_t i = 0; i < vm->stack_top; i++)
        value_free(vm->stack[i]);

    for (size_t i = 0; i < vm->frame_top; i++) {
        CallFrame *f = &vm->frames[i];
        vm_release_frame(f);
    }
    ocl_free(vm->frames);

    for (size_t i = 0; i < vm->global_count; i++)
        value_free(vm->globals[i]);
    ocl_free(vm->globals);
    value_free(vm->result);
    vm_clear_program_args(vm);

    ocl_free(vm);
}

/* ══════════════════════════════════════════════════════════════════
   Stack helpers
   ══════════════════════════════════════════════════════════════════ */

void vm_push(VM *vm, Value v) {
    if (vm->stack_top >= VM_STACK_MAX) {
        vm_error_kind(vm, ERRK_OPERATION, LOC_NONE,
                      "value stack overflow (depth=%llu, max=%d)",
                      (unsigned long long)vm->stack_top, VM_STACK_MAX);
        value_free(v);
        return;
    }
    vm->stack[vm->stack_top++] = v;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top == 0) {
        vm_error_kind(vm, ERRK_LOGIC, LOC_NONE,
                      "value stack underflow at ip=%u", vm->pc);
        return value_null();
    }
    return vm->stack[--vm->stack_top];
}

static void vm_pop_free(VM *vm) { value_free(vm_pop(vm)); }

Value vm_peek(VM *vm, size_t depth) {
    if (depth >= vm->stack_top) return value_null();
    return vm->stack[vm->stack_top - 1 - depth];
}

/* ══════════════════════════════════════════════════════════════════
   Frame helpers
   ══════════════════════════════════════════════════════════════════ */

static CallFrame *current_frame(VM *vm) {
    return vm->frame_top > 0 ? &vm->frames[vm->frame_top - 1] : NULL;
}

static void ensure_local(VM *vm, CallFrame *f, uint32_t idx) {
    (void)vm;
    if (idx < (uint32_t)f->local_capacity) {
        if (idx >= (uint32_t)f->local_count)
            f->local_count = (size_t)idx + 1;
        return;
    }
    uint32_t new_cap = idx + 16;
    f->locals = ocl_realloc(f->locals, new_cap * sizeof(OclCell *));
    for (uint32_t i = (uint32_t)f->local_capacity; i < new_cap; i++)
        f->locals[i] = ocl_cell_new(value_null());
    f->local_capacity = new_cap;
    f->local_count    = (size_t)idx + 1;
}

static void ensure_global(VM *vm, uint32_t idx) {
    if (idx < (uint32_t)vm->global_capacity) {
        if (idx >= (uint32_t)vm->global_count)
            vm->global_count = (size_t)idx + 1;
        return;
    }
    uint32_t new_cap = idx + 16;
    vm->globals = ocl_realloc(vm->globals, new_cap * sizeof(Value));
    for (uint32_t i = (uint32_t)vm->global_capacity; i < new_cap; i++)
        vm->globals[i] = value_null();
    vm->global_capacity = new_cap;
    vm->global_count    = (size_t)idx + 1;
}

/* ══════════════════════════════════════════════════════════════════
   Runtime error reporting
   ══════════════════════════════════════════════════════════════════ */

static void vm_verror_kind(VM *vm, ErrorKind kind, SourceLocation loc,
                           const char *fmt, va_list ap)
{
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);

    if (vm && vm->errors) {
        error_add(vm->errors, kind, ERROR_RUNTIME, loc, "%s", buf);
    } else {
        const char *label =
            (kind == ERRK_LOGIC)     ? "LOGIC ERROR" :
            (kind == ERRK_MEMORY)    ? "MEMORY LEAK" :
            (kind == ERRK_TYPE)      ? "TYPE ERROR" :
            (kind == ERRK_SYNTAX)    ? "SYNTAX ERROR" :
            (kind == ERRK_OPERATION) ? "OPERATION ERROR" : "ERROR";
        if (loc.filename && loc.line > 0)
            fprintf(stderr, "%s [%s:%d:%d]: %s\n",
                    label, loc.filename, loc.line, loc.column, buf);
        else
            fprintf(stderr, "%s: %s\n", label, buf);
    }

    if (vm) {
        vm->halted    = true;
        vm->exit_code = 1;
        runtime_stack_trace(vm);
    }
}

static void vm_error_kind(VM *vm, ErrorKind kind, SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vm_verror_kind(vm, kind, loc, fmt, ap);
    va_end(ap);
}

static void vm_op_error(VM *vm, SourceLocation loc, const char *fmt, ...) OCL_PRINTF_LIKE(3, 4);
static void vm_op_error(VM *vm, SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vm_verror_kind(vm, ERRK_OPERATION, loc, fmt, ap);
    va_end(ap);
}

static void vm_logic_error(VM *vm, SourceLocation loc, const char *fmt, ...) OCL_PRINTF_LIKE(3, 4);
static void vm_logic_error(VM *vm, SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vm_verror_kind(vm, ERRK_LOGIC, loc, fmt, ap);
    va_end(ap);
}

static void discard_args_push_null(VM *vm, int n) {
    for (int i = 0; i < n; i++) vm_pop_free(vm);
    vm_push(vm, value_null());
}

static void vm_store_result(VM *vm, Value value) {
    if (!vm) return;
    if (vm->has_result)
        value_free(vm->result);
    if (value.type == VALUE_STRING)
        vm->result = value_string_copy(value.data.string_val);
    else
        vm->result = value_own_copy(value);
    vm->has_result = true;
}

/* ══════════════════════════════════════════════════════════════════
   No built-in functions (disabled)
   ══════════════════════════════════════════════════════════════════ */

/* ══════════════════════════════════════════════════════════════════
   Arithmetic / comparison helpers (macros)
   ══════════════════════════════════════════════════════════════════ */

#define ARITH_OP(loc, INT_EXPR, FLOAT_EXPR)                                     \
do {                                                                            \
    Value _b = vm_pop(vm); Value _a = vm_pop(vm);                               \
    if (_a.type == VALUE_INT && _b.type == VALUE_INT) {                         \
        int64_t a = _a.data.int_val, b = _b.data.int_val; (void)a; (void)b;    \
        vm_push(vm, value_int(INT_EXPR));                                       \
    } else if ((_a.type == VALUE_INT || _a.type == VALUE_FLOAT) &&              \
               (_b.type == VALUE_INT || _b.type == VALUE_FLOAT)) {              \
        double a = (_a.type == VALUE_FLOAT) ? _a.data.float_val                \
                                            : (double)_a.data.int_val;         \
        double b = (_b.type == VALUE_FLOAT) ? _b.data.float_val                \
                                            : (double)_b.data.int_val;         \
        (void)a; (void)b;                                                       \
        vm_push(vm, value_float(FLOAT_EXPR));                                   \
    } else {                                                                    \
        vm_op_error(vm, (loc), "arithmetic requires numeric operands, got %s and %s", \
                 value_type_name(_a.type), value_type_name(_b.type));           \
        vm_push(vm, value_null());                                              \
    }                                                                           \
    value_free(_a); value_free(_b);                                             \
} while (0)

#define CMP_OP(loc, INT_EXPR, FLOAT_EXPR)                                       \
do {                                                                            \
    Value _b = vm_pop(vm); Value _a = vm_pop(vm); bool _r;                      \
    if (_a.type == VALUE_INT && _b.type == VALUE_INT) {                         \
        int64_t a = _a.data.int_val, b = _b.data.int_val; (void)a; (void)b;    \
        _r = (INT_EXPR);                                                        \
    } else if ((_a.type == VALUE_INT || _a.type == VALUE_FLOAT) &&              \
               (_b.type == VALUE_INT || _b.type == VALUE_FLOAT)) {              \
        double a = (_a.type == VALUE_FLOAT) ? _a.data.float_val                \
                                            : (double)_a.data.int_val;         \
        double b = (_b.type == VALUE_FLOAT) ? _b.data.float_val                \
                                            : (double)_b.data.int_val;         \
        (void)a; (void)b;                                                       \
        _r = (FLOAT_EXPR);                                                      \
    } else { _r = false; }                                                      \
    value_free(_a); value_free(_b);                                             \
    vm_push(vm, value_bool(_r));                                                \
} while (0)

/* ══════════════════════════════════════════════════════════════════
   Execution loop
   ══════════════════════════════════════════════════════════════════ */

int vm_execute(VM *vm) {
    if (!vm || !vm->bytecode) {
        if (vm && vm->errors)
            error_add(vm->errors, ERRK_LOGIC, ERROR_RUNTIME, LOC_NONE,
                      "vm_execute called with NULL vm or bytecode");
        else
            fprintf(stderr, "LOGIC ERROR: vm_execute called with NULL vm or bytecode\n");
        return 1;
    }

#define LOC (ins.location)

    while (!vm->halted &&
           vm->pc < (uint32_t)vm->bytecode->instruction_count)
    {
        Instruction ins = vm->bytecode->instructions[vm->pc];

        switch (ins.opcode) {

        case OP_PUSH_CONST:
            if (ins.operand1 >= (uint32_t)vm->bytecode->constant_count) {
                vm_logic_error(vm, LOC, "invalid constant index %u (pool size=%llu)",
                               ins.operand1, (unsigned long long)vm->bytecode->constant_count);
                vm_push(vm, value_null());
                break;
            }
            {
                Value c = vm->bytecode->constants[ins.operand1];
                if (c.type == VALUE_STRING)
                    vm_push(vm, value_string_borrow(c.data.string_val));
                else
                    vm_push(vm, c);
            }
            break;

        case OP_DUP: {
            Value value = vm_peek(vm, 0);
            vm_push(vm, value_own_copy(value));
            break;
        }

        case OP_POP:
            vm_pop_free(vm);
            break;

        case OP_LOAD_VAR: {
            CallFrame *f = current_frame(vm);
            if (!f) {
                vm_op_error(vm, LOC, "OP_LOAD_VAR: no active call frame");
                vm_push(vm, value_null());
                break;
            }
            if (ins.operand1 >= (uint32_t)f->local_count) {
                vm_op_error(vm, LOC, "OP_LOAD_VAR: slot %u out of bounds (frame has %llu locals)",
                            ins.operand1, (unsigned long long)f->local_count);
                vm_push(vm, value_null());
                break;
            }
            vm_push(vm, value_own_copy(f->locals[ins.operand1]->value));
            break;
        }

        case OP_STORE_VAR: {
            CallFrame *f = current_frame(vm);
            if (!f) {
                vm_op_error(vm, LOC, "OP_STORE_VAR: no active call frame");
                vm_pop_free(vm);
                break;
            }
            Value raw = vm_pop(vm);
            Value v = (raw.type == VALUE_STRING && !raw.owned)
                      ? value_string_copy(raw.data.string_val)
                      : raw;
            ensure_local(vm, f, ins.operand1);
            value_free(f->locals[ins.operand1]->value);
            f->locals[ins.operand1]->value = v;
            break;
        }

        case OP_LOAD_CAPTURE: {
            CallFrame *f = current_frame(vm);
            if (!f || !f->closure || ins.operand1 >= (uint32_t)f->closure->capture_count) {
                vm_op_error(vm, LOC, "OP_LOAD_CAPTURE: invalid capture slot %u", ins.operand1);
                vm_push(vm, value_null());
                break;
            }
            vm_push(vm, value_own_copy(f->closure->captures[ins.operand1]->value));
            break;
        }

        case OP_STORE_CAPTURE: {
            CallFrame *f = current_frame(vm);
            if (!f || !f->closure || ins.operand1 >= (uint32_t)f->closure->capture_count) {
                vm_op_error(vm, LOC, "OP_STORE_CAPTURE: invalid capture slot %u", ins.operand1);
                vm_pop_free(vm);
                break;
            }
            Value raw = vm_pop(vm);
            Value v = (raw.type == VALUE_STRING && !raw.owned)
                      ? value_string_copy(raw.data.string_val)
                      : raw;
            value_free(f->closure->captures[ins.operand1]->value);
            f->closure->captures[ins.operand1]->value = v;
            break;
        }

        case OP_LOAD_GLOBAL:
            ensure_global(vm, ins.operand1);
            {
                Value g = vm->globals[ins.operand1];
                vm_push(vm, value_own_copy(g));
            }
            break;

        case OP_STORE_GLOBAL: {
            Value raw = vm_pop(vm);
            Value v = (raw.type == VALUE_STRING && !raw.owned)
                      ? value_string_copy(raw.data.string_val)
                      : raw;
            ensure_global(vm, ins.operand1);
            value_free(vm->globals[ins.operand1]);
            vm->globals[ins.operand1] = v;
            break;
        }

        case OP_ADD: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            if (a.type == VALUE_STRING && b.type == VALUE_STRING) {
                const char *as = a.data.string_val ? a.data.string_val : "";
                const char *bs = b.data.string_val ? b.data.string_val : "";
                size_t len = strlen(as) + strlen(bs);
                char  *s   = ocl_malloc(len + 1);
                memcpy(s, as, strlen(as));
                memcpy(s + strlen(as), bs, strlen(bs) + 1);
                value_free(a); value_free(b);
                vm_push(vm, value_string(s));
            } else if (a.type == VALUE_STRING && b.type == VALUE_CHAR) {
                const char *as  = a.data.string_val ? a.data.string_val : "";
                size_t      len = strlen(as);
                char       *s   = ocl_malloc(len + 2);
                memcpy(s, as, len);
                s[len]   = b.data.char_val;
                s[len+1] = '\0';
                value_free(a); value_free(b);
                vm_push(vm, value_string(s));
            } else if (a.type == VALUE_CHAR && b.type == VALUE_STRING) {
                const char *bs  = b.data.string_val ? b.data.string_val : "";
                size_t      len = strlen(bs);
                char       *s   = ocl_malloc(len + 2);
                s[0] = a.data.char_val;
                memcpy(s + 1, bs, len + 1);
                value_free(a); value_free(b);
                vm_push(vm, value_string(s));
            } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                int64_t r = a.data.int_val + b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_int(r));
            } else if ((a.type == VALUE_INT || a.type == VALUE_FLOAT) &&
                       (b.type == VALUE_INT || b.type == VALUE_FLOAT)) {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_float(af + bf));
            } else {
                vm_op_error(vm, LOC, "'+' cannot combine %s and %s",
                         value_type_name(a.type), value_type_name(b.type));
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            }
            break;
        }

        case OP_SUBTRACT: ARITH_OP(LOC, a - b, a - b); break;

        case OP_MULTIPLY: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            /* String repetition: "abc" * 3 or 3 * "abc" */
            if ((a.type == VALUE_STRING && b.type == VALUE_INT) ||
                (a.type == VALUE_INT && b.type == VALUE_STRING)) {
                const char *src = (a.type == VALUE_STRING) ? a.data.string_val : b.data.string_val;
                int64_t count   = (a.type == VALUE_INT)    ? a.data.int_val    : b.data.int_val;
                if (!src) src = "";
                if (count <= 0) {
                    value_free(a); value_free(b);
                    vm_push(vm, value_string(ocl_strdup("")));
                } else {
                    size_t slen = strlen(src);
                    size_t total = slen * (size_t)count;
                    char *s = ocl_malloc(total + 1);
                    for (int64_t i = 0; i < count; i++)
                        memcpy(s + (size_t)i * slen, src, slen);
                    s[total] = '\0';
                    value_free(a); value_free(b);
                    vm_push(vm, value_string(s));
                }
            } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                int64_t r = a.data.int_val * b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_int(r));
            } else if ((a.type == VALUE_INT || a.type == VALUE_FLOAT) &&
                       (b.type == VALUE_INT || b.type == VALUE_FLOAT)) {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_float(af * bf));
            } else {
                vm_op_error(vm, LOC, "'*' cannot combine %s and %s",
                         value_type_name(a.type), value_type_name(b.type));
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            }
            break;
        }

        case OP_DIVIDE: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            bool  div_by_zero = (b.type == VALUE_FLOAT) ? (b.data.float_val == 0.0)
                                                        : (b.data.int_val   == 0);
            if (div_by_zero) {
                vm_op_error(vm, LOC, "division by zero");
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                int64_t r = a.data.int_val / b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_int(r));
            } else {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_float(af / bf));
            }
            break;
        }

        case OP_MODULO: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            if (a.type == VALUE_INT && b.type == VALUE_INT) {
                if (b.data.int_val == 0) {
                    vm_op_error(vm, LOC, "modulo by zero");
                    value_free(a); value_free(b);
                    vm_push(vm, value_null());
                } else {
                    int64_t r = a.data.int_val % b.data.int_val;
                    value_free(a); value_free(b);
                    vm_push(vm, value_int(r));
                }
            } else if ((a.type == VALUE_INT || a.type == VALUE_FLOAT) &&
                       (b.type == VALUE_INT || b.type == VALUE_FLOAT)) {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                if (bf == 0.0) {
                    vm_op_error(vm, LOC, "modulo by zero");
                    value_free(a); value_free(b);
                    vm_push(vm, value_null());
                } else {
                    value_free(a); value_free(b);
                    vm_push(vm, value_float(fmod(af, bf)));
                }
            } else {
                vm_op_error(vm, LOC, "'%%' requires numeric operands, got %s and %s",
                         value_type_name(a.type), value_type_name(b.type));
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            }
            break;
        }

        case OP_NEGATE: {
            Value a = vm_pop(vm);
            if (a.type == VALUE_INT)
                vm_push(vm, value_int(-a.data.int_val));
            else if (a.type == VALUE_FLOAT)
                vm_push(vm, value_float(-a.data.float_val));
            else {
                vm_op_error(vm, LOC, "unary '-' on %s", value_type_name(a.type));
                vm_push(vm, value_null());
            }
            value_free(a);
            break;
        }

        case OP_NOT: {
            Value a = vm_pop(vm);
            bool  r = !value_is_truthy(a);
            value_free(a);
            vm_push(vm, value_bool(r));
            break;
        }

        case OP_BIT_NOT: {
            Value a = vm_pop(vm);
            if (a.type != VALUE_INT) {
                vm_op_error(vm, LOC, "bitwise '~' requires Int, got %s",
                         value_type_name(a.type));
                value_free(a);
                vm_push(vm, value_null());
                break;
            }
            vm_push(vm, value_int(~a.data.int_val));
            value_free(a);
            break;
        }

        case OP_EQUAL: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            bool  r = false;
            if (a.type == b.type) {
                switch (a.type) {
                    case VALUE_INT:    r = a.data.int_val    == b.data.int_val;   break;
                    case VALUE_FLOAT:  r = a.data.float_val  == b.data.float_val; break;
                    case VALUE_BOOL:   r = a.data.bool_val   == b.data.bool_val;  break;
                    case VALUE_CHAR:   r = a.data.char_val   == b.data.char_val;  break;
                    case VALUE_STRING:
                        r = (a.data.string_val && b.data.string_val)
                            ? strcmp(a.data.string_val, b.data.string_val) == 0
                            : a.data.string_val == b.data.string_val;
                        break;
                    case VALUE_ARRAY:
                        r = a.data.array_val == b.data.array_val;
                        break;
                    case VALUE_STRUCT:
                        r = a.data.struct_val == b.data.struct_val;
                        break;
                    case VALUE_NULL:   r = true;  break;
                    default: break;
                }
            } else if ((a.type == VALUE_INT || a.type == VALUE_FLOAT) &&
                       (b.type == VALUE_INT || b.type == VALUE_FLOAT)) {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                r = (af == bf);
            }
            value_free(a); value_free(b);
            vm_push(vm, value_bool(r));
            break;
        }

        case OP_NOT_EQUAL: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            bool  r = true;
            if (a.type == b.type) {
                switch (a.type) {
                    case VALUE_INT:    r = a.data.int_val    != b.data.int_val;   break;
                    case VALUE_FLOAT:  r = a.data.float_val  != b.data.float_val; break;
                    case VALUE_BOOL:   r = a.data.bool_val   != b.data.bool_val;  break;
                    case VALUE_CHAR:   r = a.data.char_val   != b.data.char_val;  break;
                    case VALUE_STRING:
                        r = (a.data.string_val && b.data.string_val)
                            ? strcmp(a.data.string_val, b.data.string_val) != 0
                            : a.data.string_val != b.data.string_val;
                        break;
                    case VALUE_ARRAY:
                        r = a.data.array_val != b.data.array_val;
                        break;
                    case VALUE_STRUCT:
                        r = a.data.struct_val != b.data.struct_val;
                        break;
                    case VALUE_NULL:   r = false; break;
                    default: break;
                }
            } else if ((a.type == VALUE_INT || a.type == VALUE_FLOAT) &&
                       (b.type == VALUE_INT || b.type == VALUE_FLOAT)) {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                r = (af != bf);
            }
            value_free(a); value_free(b);
            vm_push(vm, value_bool(r));
            break;
        }

        case OP_LESS:
        case OP_LESS_EQUAL:
        case OP_GREATER:
        case OP_GREATER_EQUAL: {
            Value b = vm_pop(vm); Value a = vm_pop(vm); bool _r;
            if (a.type == VALUE_STRING && b.type == VALUE_STRING) {
                const char *as = a.data.string_val ? a.data.string_val : "";
                const char *bs = b.data.string_val ? b.data.string_val : "";
                int cmp = strcmp(as, bs);
                switch (ins.opcode) {
                    case OP_LESS:          _r = cmp < 0;  break;
                    case OP_LESS_EQUAL:    _r = cmp <= 0; break;
                    case OP_GREATER:       _r = cmp > 0;  break;
                    case OP_GREATER_EQUAL: _r = cmp >= 0; break;
                    default: _r = false; break;
                }
            } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                int64_t av = a.data.int_val, bv = b.data.int_val;
                switch (ins.opcode) {
                    case OP_LESS:          _r = av < bv;  break;
                    case OP_LESS_EQUAL:    _r = av <= bv; break;
                    case OP_GREATER:       _r = av > bv;  break;
                    case OP_GREATER_EQUAL: _r = av >= bv; break;
                    default: _r = false; break;
                }
            } else if ((a.type == VALUE_INT || a.type == VALUE_FLOAT) &&
                       (b.type == VALUE_INT || b.type == VALUE_FLOAT)) {
                double af = (a.type == VALUE_FLOAT) ? a.data.float_val : (double)a.data.int_val;
                double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : (double)b.data.int_val;
                switch (ins.opcode) {
                    case OP_LESS:          _r = af < bf;  break;
                    case OP_LESS_EQUAL:    _r = af <= bf; break;
                    case OP_GREATER:       _r = af > bf;  break;
                    case OP_GREATER_EQUAL: _r = af >= bf; break;
                    default: _r = false; break;
                }
            } else { _r = false; }
            value_free(a); value_free(b);
            vm_push(vm, value_bool(_r));
            break;
        }

        case OP_AND: {
            Value b = vm_pop(vm); Value a = vm_pop(vm);
            bool  r = value_is_truthy(a) && value_is_truthy(b);
            value_free(a); value_free(b);
            vm_push(vm, value_bool(r));
            break;
        }
        case OP_OR: {
            Value b = vm_pop(vm); Value a = vm_pop(vm);
            bool  r = value_is_truthy(a) || value_is_truthy(b);
            value_free(a); value_free(b);
            vm_push(vm, value_bool(r));
            break;
        }

        case OP_BIT_AND:
        case OP_BIT_OR:
        case OP_BIT_XOR:
        case OP_LSHIFT:
        case OP_RSHIFT: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            if (a.type != VALUE_INT || b.type != VALUE_INT) {
                vm_op_error(vm, LOC, "bitwise operators require Int operands, got %s and %s",
                         value_type_name(a.type), value_type_name(b.type));
                value_free(a);
                value_free(b);
                vm_push(vm, value_null());
                break;
            }

            int64_t result = 0;
            if ((ins.opcode == OP_LSHIFT || ins.opcode == OP_RSHIFT) &&
                (b.data.int_val < 0 || b.data.int_val >= 64)) {
                vm_op_error(vm, LOC, "shift count must be between 0 and 63, got %" PRId64,
                            b.data.int_val);
                value_free(a);
                value_free(b);
                vm_push(vm, value_null());
                break;
            }

            switch (ins.opcode) {
                case OP_BIT_AND: result = a.data.int_val & b.data.int_val; break;
                case OP_BIT_OR:  result = a.data.int_val | b.data.int_val; break;
                case OP_BIT_XOR: result = a.data.int_val ^ b.data.int_val; break;
                case OP_LSHIFT:  result = a.data.int_val << b.data.int_val; break;
                case OP_RSHIFT:  result = a.data.int_val >> b.data.int_val; break;
                default: break;
            }

            value_free(a);
            value_free(b);
            vm_push(vm, value_int(result));
            break;
        }

        case OP_JUMP:
            vm->pc = ins.operand1;
            continue;

        case OP_JUMP_IF_FALSE: {
            Value cond  = vm_pop(vm);
            bool  taken = !value_is_truthy(cond);
            value_free(cond);
            if (taken) { vm->pc = ins.operand1; continue; }
            break;
        }

        case OP_JUMP_IF_TRUE: {
            Value cond  = vm_pop(vm);
            bool  taken = value_is_truthy(cond);
            value_free(cond);
            if (taken) { vm->pc = ins.operand1; continue; }
            break;
        }

        case OP_JUMP_IF_NOT_NULL: {
            Value cond = vm_pop(vm);
            bool taken = cond.type != VALUE_NULL;
            value_free(cond);
            if (taken) { vm->pc = ins.operand1; continue; }
            break;
        }

        case OP_CALL: {
            uint32_t fidx = ins.operand1;
            uint32_t argc = ins.operand2;

            if (fidx == 0xFFFFFFFF) {
                vm_op_error(vm, LOC, "call to unresolved function");
                discard_args_push_null(vm, (int)argc);
                break;
            }
            if (fidx >= (uint32_t)vm->bytecode->function_count) {
                vm_logic_error(vm, LOC, "invalid function index %u (only %llu defined)",
                               fidx, (unsigned long long)vm->bytecode->function_count);
                discard_args_push_null(vm, (int)argc);
                break;
            }

            FuncEntry *fe = &vm->bytecode->functions[fidx];

            if (!vm_accepts_argc(fe->param_count, argc)) {
                if (OCL_ARGS_VARIADIC(fe->param_count))
                    vm_op_error(vm, LOC,
                             "function '%s' expects at least %d argument(s), got %u",
                             fe->name ? fe->name : "?", OCL_ARGS_MIN(fe->param_count), argc);
                else
                    vm_op_error(vm, LOC,
                             "function '%s' expects %d argument(s), got %u",
                             fe->name ? fe->name : "?", fe->param_count, argc);
                discard_args_push_null(vm, (int)argc);
                break;
            }

            if (fe->start_ip == OCL_FUNC_BUILTIN) {
                const StdlibEntry *entry = stdlib_lookup_by_name(fe->name);
                if (!entry || !stdlib_dispatch(vm, entry->id, (int)argc)) {
                    vm_op_error(vm, LOC, "call to unknown builtin '%s'",
                                fe->name ? fe->name : "?");
                    discard_args_push_null(vm, (int)argc);
                }
                break;
            }

            if (vm->frame_top >= VM_FRAMES_MAX) {
                vm_op_error(vm, LOC,
                         "call stack overflow (max %d frames, called '%s')",
                         VM_FRAMES_MAX, fe->name ? fe->name : "?");
                discard_args_push_null(vm, (int)argc);
                break;
            }
            if (vm->frame_top >= vm->frame_capacity) {
                size_t new_cap = vm->frame_capacity * VM_FRAMES_GROW;
                if (new_cap > VM_FRAMES_MAX) new_cap = VM_FRAMES_MAX;
                vm->frames        = ocl_realloc(vm->frames, new_cap * sizeof(CallFrame));
                vm->frame_capacity = new_cap;
            }

            CallFrame *frame = &vm->frames[vm->frame_top++];
            frame->return_ip  = vm->pc + 1;
            frame->stack_base = (uint32_t)vm->stack_top;
            frame->function_index = fidx;
            frame->closure = NULL;

            int local_cap = (fe->local_count > (int)argc ? fe->local_count : (int)argc) + 8;
            frame->locals          = ocl_malloc((size_t)local_cap * sizeof(OclCell *));
            frame->local_count     = (size_t)(fe->local_count > (int)argc ? fe->local_count : (int)argc);
            frame->local_capacity  = (size_t)local_cap;
            for (int i = 0; i < local_cap; i++)
                frame->locals[i] = ocl_cell_new(value_null());

            for (int i = (int)argc - 1; i >= 0; i--) {
                Value popped = vm_pop(vm);
                Value stored = (popped.type == VALUE_STRING && !popped.owned)
                               ? value_string_copy(popped.data.string_val)
                               : popped;
                value_free(frame->locals[i]->value);
                frame->locals[i]->value = stored;
            }

            vm->pc = fe->start_ip;
            continue;
        }

        case OP_CALL_VALUE: {
            uint32_t argc = ins.operand2;
            Value *args = argc > 0 ? ocl_malloc((size_t)argc * sizeof(Value)) : NULL;
            for (int i = (int)argc - 1; i >= 0; i--)
                args[i] = vm_pop(vm);
            Value callee = vm_pop(vm);

            if (callee.type != VALUE_FUNCTION || !callee.data.function_val) {
                vm_op_error(vm, LOC, "call target is not a function");
                for (uint32_t i = 0; i < argc; i++) value_free(args[i]);
                ocl_free(args);
                value_free(callee);
                vm_push(vm, value_null());
                break;
            }

            uint32_t fidx = callee.data.function_val->function_index;
            if (fidx >= (uint32_t)vm->bytecode->function_count) {
                vm_logic_error(vm, LOC, "invalid function index %u", fidx);
                for (uint32_t i = 0; i < argc; i++) value_free(args[i]);
                ocl_free(args);
                value_free(callee);
                vm_push(vm, value_null());
                break;
            }

            FuncEntry *fe = &vm->bytecode->functions[fidx];
            if (!vm_accepts_argc(fe->param_count, argc)) {
                vm_op_error(vm, LOC, "function value '%s' expects %d argument(s), got %u",
                            fe->name ? fe->name : "?",
                            fe->param_count,
                            argc);
                for (uint32_t i = 0; i < argc; i++) value_free(args[i]);
                ocl_free(args);
                value_free(callee);
                vm_push(vm, value_null());
                break;
            }

            if (fe->start_ip == OCL_FUNC_BUILTIN) {
                for (uint32_t i = 0; i < argc; i++)
                    vm_push(vm, args[i]);
                ocl_free(args);
                const StdlibEntry *entry = stdlib_lookup_by_name(fe->name);
                value_free(callee);
                if (!entry || !stdlib_dispatch(vm, entry->id, (int)argc)) {
                    vm_op_error(vm, LOC, "call to unknown builtin '%s'",
                                fe->name ? fe->name : "?");
                    discard_args_push_null(vm, (int)argc);
                }
                break;
            }

            if (vm->frame_top >= VM_FRAMES_MAX) {
                vm_op_error(vm, LOC, "call stack overflow (max %d frames)", VM_FRAMES_MAX);
                for (uint32_t i = 0; i < argc; i++) value_free(args[i]);
                ocl_free(args);
                value_free(callee);
                vm_push(vm, value_null());
                break;
            }
            if (vm->frame_top >= vm->frame_capacity) {
                size_t new_cap = vm->frame_capacity * VM_FRAMES_GROW;
                if (new_cap > VM_FRAMES_MAX) new_cap = VM_FRAMES_MAX;
                vm->frames = ocl_realloc(vm->frames, new_cap * sizeof(CallFrame));
                vm->frame_capacity = new_cap;
            }

            CallFrame *frame = &vm->frames[vm->frame_top++];
            frame->return_ip = vm->pc + 1;
            frame->stack_base = (uint32_t)vm->stack_top;
            frame->function_index = fidx;
            frame->closure = callee.data.function_val;
            ocl_function_retain(frame->closure);

            int local_cap = (fe->local_count > (int)argc ? fe->local_count : (int)argc) + 8;
            frame->locals = ocl_malloc((size_t)local_cap * sizeof(OclCell *));
            frame->local_count = (size_t)(fe->local_count > (int)argc ? fe->local_count : (int)argc);
            frame->local_capacity = (size_t)local_cap;
            for (int i = 0; i < local_cap; i++)
                frame->locals[i] = ocl_cell_new(value_null());
            for (uint32_t i = 0; i < argc; i++) {
                value_free(frame->locals[i]->value);
                frame->locals[i]->value = (args[i].type == VALUE_STRING && !args[i].owned)
                                          ? value_string_copy(args[i].data.string_val)
                                          : args[i];
            }

            ocl_free(args);
            value_free(callee);
            vm->pc = fe->start_ip;
            continue;
        }

        case OP_MAKE_FUNCTION: {
            if (ins.operand1 >= (uint32_t)vm->bytecode->function_count) {
                vm_logic_error(vm, LOC, "invalid function index %u", ins.operand1);
                vm_push(vm, value_null());
                break;
            }

            FuncEntry *fe = &vm->bytecode->functions[ins.operand1];
            OclFunction *fn = ocl_function_new(ins.operand1, fe->capture_count);
            CallFrame *frame = current_frame(vm);

            for (size_t i = 0; i < fe->capture_count; i++) {
                OclCell *cell = NULL;
                if (fe->captures[i].source == FUNC_CAPTURE_LOCAL) {
                    if (!frame || fe->captures[i].slot >= frame->local_count) {
                        vm_logic_error(vm, LOC, "invalid local capture slot %u", fe->captures[i].slot);
                        continue;
                    }
                    cell = frame->locals[fe->captures[i].slot];
                } else {
                    if (!frame || !frame->closure || fe->captures[i].slot >= frame->closure->capture_count) {
                        vm_logic_error(vm, LOC, "invalid outer capture slot %u", fe->captures[i].slot);
                        continue;
                    }
                    cell = frame->closure->captures[fe->captures[i].slot];
                }
                fn->captures[i] = cell;
                ocl_cell_retain(cell);
            }

            vm_push(vm, value_function(fn));
            ocl_function_release(fn);
            break;
        }

        case OP_RETURN: {
            Value ret_raw = vm_pop(vm);

            if (vm->frame_top == 0) {
                vm_store_result(vm, ret_raw);
                value_free(ret_raw);
                vm->halted = true;
                break;
            }

            Value ret = (ret_raw.type == VALUE_STRING && !ret_raw.owned)
                        ? value_string_copy(ret_raw.data.string_val)
                        : ret_raw;

            CallFrame *frame = &vm->frames[--vm->frame_top];
            /* Save frame fields before releasing — vm_release_frame frees
               the locals array and closure, which could invalidate data
               we still need (stack_base, return_ip). */
            uint32_t   ret_ip     = frame->return_ip;
            uint32_t   stack_base = frame->stack_base;

            vm_release_frame(frame);

            while (vm->stack_top > stack_base)
                vm_pop_free(vm);

            vm_push(vm, ret);
            vm->pc = ret_ip;
            continue;
        }

        case OP_HALT:
            vm->halted = true;
            if (vm->stack_top > 0)
                vm_store_result(vm, vm_peek(vm, 0));
            break;

        case OP_ARRAY_NEW: {
            uint32_t count = ins.operand1;
            OclArray *arr  = ocl_array_new(count > 0 ? (size_t)count : 8);

            if (count > 0) {
                Value *tmp = ocl_malloc((size_t)count * sizeof(Value));
                for (uint32_t i = count; i-- > 0; )
                    tmp[i] = vm_pop(vm);
                for (uint32_t i = 0; i < count; i++) {
                    ocl_array_push(arr, tmp[i]);
                    value_free(tmp[i]);
                }
                ocl_free(tmp);
            }

            vm_push(vm, value_array(arr));
            ocl_array_release(arr);
            break;
        }

        case OP_ARRAY_GET: {
            Value idx_v = vm_pop(vm);
            Value arr_v = vm_pop(vm);

            if (arr_v.type == VALUE_STRING) {
                if (idx_v.type != VALUE_INT) {
                    vm_op_error(vm, LOC, "string index must be Int, got %s",
                             value_type_name(idx_v.type));
                    value_free(idx_v); value_free(arr_v);
                    vm_push(vm, value_null());
                    break;
                }
                const char *s    = arr_v.data.string_val ? arr_v.data.string_val : "";
                int64_t     idx  = idx_v.data.int_val;
                size_t      slen = strlen(s);
                /* Support negative indexing: -1 = last element */
                if (idx < 0) idx += (int64_t)slen;
                if (idx < 0 || (size_t)idx >= slen) {
                    vm_op_error(vm, LOC,
                                "string index %"PRId64" out of bounds [0, %llu)",
                                idx_v.data.int_val, (unsigned long long)slen);
                    value_free(idx_v); value_free(arr_v);
                    vm_push(vm, value_null());
                    break;
                }
                char ch = s[idx];
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, value_char(ch));
                break;
            }

            if (arr_v.type != VALUE_ARRAY || !arr_v.data.array_val) {
                vm_op_error(vm, LOC, "index access on non-indexable type %s",
                         value_type_name(arr_v.type));
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, value_null());
                break;
            }
            if (idx_v.type != VALUE_INT) {
                vm_op_error(vm, LOC, "array index must be Int, got %s",
                         value_type_name(idx_v.type));
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, value_null());
                break;
            }
            {
                int64_t   idx = idx_v.data.int_val;
                OclArray *arr = arr_v.data.array_val;
                /* Support negative indexing: -1 = last element */
                if (idx < 0) idx += (int64_t)arr->length;
                if (idx < 0 || (size_t)idx >= arr->length) {
                    vm_op_error(vm, LOC,
                                "array index %"PRId64" out of bounds [0, %llu)",
                                idx_v.data.int_val, (unsigned long long)arr->length);
                    value_free(idx_v); value_free(arr_v);
                    vm_push(vm, value_null());
                    break;
                }
                Value elem = ocl_array_get(arr, (size_t)idx);
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, elem);
            }
            break;
        }

        case OP_ARRAY_SET: {
            Value idx_v = vm_pop(vm);
            Value arr_v = vm_pop(vm);
            Value val   = vm_pop(vm);

            if (arr_v.type != VALUE_ARRAY || !arr_v.data.array_val) {
                vm_op_error(vm, LOC, "OP_ARRAY_SET on non-Array type %s",
                         value_type_name(arr_v.type));
                value_free(idx_v); value_free(arr_v); value_free(val);
                break;
            }
            if (idx_v.type != VALUE_INT) {
                vm_op_error(vm, LOC, "array index must be Int, got %s",
                         value_type_name(idx_v.type));
                value_free(idx_v); value_free(arr_v); value_free(val);
                break;
            }
            int64_t idx = idx_v.data.int_val;
            /* Support negative indexing: -1 = last element */
            if (idx < 0) idx += (int64_t)arr_v.data.array_val->length;
            if (idx < 0) {
                vm_op_error(vm, LOC, "array index %"PRId64" out of bounds", idx_v.data.int_val);
                value_free(idx_v); value_free(arr_v); value_free(val);
                break;
            }
            ocl_array_set(arr_v.data.array_val, (size_t)idx, val);
            value_free(idx_v); value_free(arr_v); value_free(val);
            break;
        }

        case OP_ARRAY_LEN: {
            Value arr_v = vm_pop(vm);
            int64_t len;
            if (arr_v.type == VALUE_STRING) {
                len = (int64_t)(arr_v.data.string_val
                                ? strlen(arr_v.data.string_val) : 0);
            } else if (arr_v.type == VALUE_ARRAY && arr_v.data.array_val) {
                len = (int64_t)arr_v.data.array_val->length;
            } else {
                vm_op_error(vm, LOC, "arrayLen requires Array or String, got %s",
                         value_type_name(arr_v.type));
                len = 0;
            }
            value_free(arr_v);
            vm_push(vm, value_int(len));
            break;
        }

        case OP_STRUCT_NEW: {
            const char *type_name = "Struct";
            bool ok = true;

            if (ins.operand1 < (uint32_t)vm->bytecode->constant_count) {
                Value type_v = vm->bytecode->constants[ins.operand1];
                if (type_v.type == VALUE_STRING && type_v.data.string_val)
                    type_name = type_v.data.string_val;
                else
                    ok = false;
            } else {
                ok = false;
            }

            if (!ok)
                vm_logic_error(vm, LOC, "invalid struct type constant %u", ins.operand1);

            OclStruct *s = ocl_struct_new(type_name, ins.operand2);
            /* Pop fields into a temporary array first, then insert in
               forward order so the struct fields match the source order. */
            if (ins.operand2 > 0) {
                typedef struct { Value name_v; Value value_v; } FieldPair;
                FieldPair *pairs = ocl_malloc(ins.operand2 * sizeof(FieldPair));
                for (uint32_t i = ins.operand2; i-- > 0; ) {
                    pairs[i].value_v = vm_pop(vm);
                    pairs[i].name_v  = vm_pop(vm);
                }
                for (uint32_t i = 0; i < ins.operand2; i++) {
                    if (pairs[i].name_v.type != VALUE_STRING || !pairs[i].name_v.data.string_val) {
                        vm_op_error(vm, LOC, "struct field name must be String");
                        ok = false;
                    } else if (!ocl_struct_set(s, pairs[i].name_v.data.string_val, pairs[i].value_v)) {
                        vm_op_error(vm, LOC, "failed to set struct field '%s'", pairs[i].name_v.data.string_val);
                        ok = false;
                    }
                    value_free(pairs[i].value_v);
                    value_free(pairs[i].name_v);
                }
                ocl_free(pairs);
            }

            if (ok)
                vm_push(vm, value_struct(s));
            else
                vm_push(vm, value_null());
            ocl_struct_release(s);
            break;
        }

        case OP_STRUCT_GET: {
            Value obj_v = vm_pop(vm);
            Value result = value_null();
            bool found = false;

            if (ins.operand1 >= (uint32_t)vm->bytecode->constant_count) {
                vm_logic_error(vm, LOC, "invalid struct field constant %u", ins.operand1);
                value_free(obj_v);
                vm_push(vm, value_null());
                break;
            }

            Value field_v = vm->bytecode->constants[ins.operand1];
            if (field_v.type != VALUE_STRING || !field_v.data.string_val) {
                vm_logic_error(vm, LOC, "struct field constant %u is not a String", ins.operand1);
                value_free(obj_v);
                vm_push(vm, value_null());
                break;
            }

            if (obj_v.type != VALUE_STRUCT || !obj_v.data.struct_val) {
                vm_op_error(vm, LOC, "field access requires Struct, got %s",
                         value_type_name(obj_v.type));
                value_free(obj_v);
                vm_push(vm, value_null());
                break;
            }

            result = ocl_struct_get(obj_v.data.struct_val, field_v.data.string_val, &found);
            if (!found)
                vm_op_error(vm, LOC, "struct has no field '%s'", field_v.data.string_val);
            value_free(obj_v);
            vm_push(vm, result);
            break;
        }

        case OP_STRUCT_SET: {
            Value obj_v = vm_pop(vm);
            Value value_v = vm_pop(vm);

            if (ins.operand1 >= (uint32_t)vm->bytecode->constant_count) {
                vm_logic_error(vm, LOC, "invalid struct field constant %u", ins.operand1);
                value_free(obj_v);
                value_free(value_v);
                break;
            }

            Value field_v = vm->bytecode->constants[ins.operand1];
            if (field_v.type != VALUE_STRING || !field_v.data.string_val) {
                vm_logic_error(vm, LOC, "struct field constant %u is not a String", ins.operand1);
                value_free(obj_v);
                value_free(value_v);
                break;
            }

            if (obj_v.type != VALUE_STRUCT || !obj_v.data.struct_val) {
                vm_op_error(vm, LOC, "field assignment requires Struct, got %s",
                         value_type_name(obj_v.type));
                value_free(obj_v);
                value_free(value_v);
                break;
            }

            if (!ocl_struct_set(obj_v.data.struct_val, field_v.data.string_val, value_v))
                vm_op_error(vm, LOC, "failed to set struct field '%s'", field_v.data.string_val);
            value_free(obj_v);
            value_free(value_v);
            break;
        }

        default:
            vm_op_error(vm, LOC, "unknown opcode %d at ip=%u",
                     (int)ins.opcode, vm->pc);
            break;
        }

        vm->pc++;
    }

#undef LOC
#undef ARITH_OP
#undef CMP_OP

    return vm->exit_code;
}
