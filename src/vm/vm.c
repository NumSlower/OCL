#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "vm.h"
#include "ocl_stdlib.h"
#include "common.h"

/* ══════════════════════════════════════════════════════════════════
   Lifecycle
   ══════════════════════════════════════════════════════════════════ */

VM *vm_create(Bytecode *bytecode) {
    if (!bytecode) {
        fprintf(stderr, "ocl: vm_create: NULL bytecode\n");
        return NULL;
    }
    VM *vm = ocl_malloc(sizeof(VM));
    *vm = (VM){0};

    vm->bytecode       = bytecode;
    vm->frame_capacity = VM_FRAMES_INITIAL;
    vm->frames         = ocl_malloc(vm->frame_capacity * sizeof(CallFrame));
    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;

    for (size_t i = 0; i < vm->stack_top; i++)
        value_free(vm->stack[i]);

    for (size_t i = 0; i < vm->frame_top; i++) {
        CallFrame *f = &vm->frames[i];
        for (size_t j = 0; j < f->local_count; j++)
            value_free(f->locals[j]);
        ocl_free(f->locals);
    }
    ocl_free(vm->frames);

    for (size_t i = 0; i < vm->global_count; i++)
        value_free(vm->globals[i]);
    ocl_free(vm->globals);

    ocl_free(vm);
}

/* ══════════════════════════════════════════════════════════════════
   Stack helpers
   ══════════════════════════════════════════════════════════════════ */

void vm_push(VM *vm, Value v) {
    if (vm->stack_top >= VM_STACK_MAX) {
        fprintf(stderr,
                "RUNTIME ERROR: value stack overflow (depth=%zu, max=%d)\n",
                vm->stack_top, VM_STACK_MAX);
        value_free(v);
        vm->halted    = true;
        vm->exit_code = 1;
        return;
    }
    vm->stack[vm->stack_top++] = v;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top == 0) {
        fprintf(stderr,
                "RUNTIME ERROR: value stack underflow at ip=%u\n", vm->pc);
        vm->halted    = true;
        vm->exit_code = 1;
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
    f->locals = ocl_realloc(f->locals, new_cap * sizeof(Value));
    for (uint32_t i = (uint32_t)f->local_capacity; i < new_cap; i++)
        f->locals[i] = value_null();
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

static void vm_error(VM *vm, SourceLocation loc,
                     const char *fmt, ...) OCL_PRINTF_LIKE(3, 4);

static void vm_error(VM *vm, SourceLocation loc, const char *fmt, ...) {
    if (loc.filename && loc.line > 0)
        fprintf(stderr, "RUNTIME ERROR [%s:%d:%d]: ",
                loc.filename, loc.line, loc.column);
    else
        fprintf(stderr, "RUNTIME ERROR: ");

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    vm->halted    = true;
    vm->exit_code = 1;
}

static void discard_args_push_null(VM *vm, int n) {
    for (int i = 0; i < n; i++) vm_pop_free(vm);
    vm_push(vm, value_null());
}

/* ══════════════════════════════════════════════════════════════════
   Built-in print / printf
   ══════════════════════════════════════════════════════════════════ */

static Value *pop_n(VM *vm, int n) {
    if (n <= 0) return NULL;
    Value *buf = ocl_malloc((size_t)n * sizeof(Value));
    for (int i = n - 1; i >= 0; i--)
        buf[i] = vm_pop(vm);
    return buf;
}

static void free_n(Value *buf, int n) {
    for (int i = 0; i < n; i++) value_free(buf[i]);
    ocl_free(buf);
}

static void builtin_print(VM *vm, int argc) {
    Value *args = pop_n(vm, argc);
    for (int i = 0; i < argc; i++) {
        if (i > 0) putchar(' ');
        switch (args[i].type) {
            case VALUE_INT:    printf("%"PRId64, args[i].data.int_val);         break;
            case VALUE_FLOAT:  printf("%g",       args[i].data.float_val);       break;
            case VALUE_STRING: printf("%s",       args[i].data.string_val
                                                  ? args[i].data.string_val : ""); break;
            case VALUE_BOOL:   printf("%s",       args[i].data.bool_val
                                                  ? "true" : "false");           break;
            case VALUE_CHAR:   printf("%c",       args[i].data.char_val);        break;
            case VALUE_NULL:   printf("null");                                   break;
            case VALUE_ARRAY:  printf("%s",       value_to_string(args[i]));     break;
        }
    }
    putchar('\n');
    free_n(args, argc);
    vm_push(vm, value_null());
}

static void builtin_printf(VM *vm, int argc) {
    if (argc < 1) { vm_push(vm, value_null()); return; }

    Value *args = pop_n(vm, argc);

    if (args[0].type != VALUE_STRING) {
        printf("%s", value_to_string(args[0]));
        free_n(args, argc);
        vm_push(vm, value_null());
        return;
    }

    const char *fmt     = args[0].data.string_val ? args[0].data.string_val : "";
    int         arg_idx = 1;

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] == '\\' && fmt[i + 1]) {
            switch (fmt[++i]) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case '\\': putchar('\\'); break;
                default:   putchar(fmt[i]); break;
            }
        } else if (fmt[i] == '%' && fmt[i + 1]) {
            char spec = fmt[++i];
            if (arg_idx >= argc && spec != '%') {
                putchar('%');
                putchar(spec);
                continue;
            }
            Value a = (spec != '%') ? args[arg_idx++] : value_null();
            switch (spec) {
                case 'd': case 'i':
                    if (a.type == VALUE_INT)
                        printf("%"PRId64, a.data.int_val);
                    else if (a.type == VALUE_FLOAT)
                        printf("%"PRId64, (int64_t)a.data.float_val);
                    else
                        printf("%s", value_to_string(a));
                    break;
                case 'f':
                    if (a.type == VALUE_FLOAT)
                        printf("%g", a.data.float_val);
                    else if (a.type == VALUE_INT)
                        printf("%g", (double)a.data.int_val);
                    else
                        printf("%s", value_to_string(a));
                    break;
                case 's':
                    printf("%s", value_to_string(a));
                    break;
                case 'c':
                    if (a.type == VALUE_CHAR) putchar(a.data.char_val);
                    else printf("%s", value_to_string(a));
                    break;
                case 'b':
                    printf("%s", value_is_truthy(a) ? "true" : "false");
                    break;
                case '%':
                    putchar('%');
                    break;
                default:
                    putchar('%');
                    putchar(spec);
                    break;
            }
        } else {
            putchar(fmt[i]);
        }
    }

    free_n(args, argc);
    vm_push(vm, value_null());
}

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
        vm_error(vm, (loc), "arithmetic requires numeric operands, got %s and %s", \
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
        fprintf(stderr, "RUNTIME ERROR: vm_execute called with NULL vm or bytecode\n");
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
                vm_error(vm, LOC, "invalid constant index %u (pool size=%zu)",
                         ins.operand1, vm->bytecode->constant_count);
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

        case OP_POP:
            vm_pop_free(vm);
            break;

        case OP_LOAD_VAR: {
            CallFrame *f = current_frame(vm);
            if (!f) {
                vm_error(vm, LOC, "OP_LOAD_VAR: no active call frame");
                vm_push(vm, value_null());
                break;
            }
            if (ins.operand1 >= (uint32_t)f->local_count) {
                vm_error(vm, LOC, "OP_LOAD_VAR: slot %u out of bounds (frame has %zu locals)",
                         ins.operand1, f->local_count);
                vm_push(vm, value_null());
                break;
            }
            Value v = f->locals[ins.operand1];
            if (v.type == VALUE_STRING)
                vm_push(vm, value_string_borrow(v.data.string_val));
            else if (v.type == VALUE_ARRAY)
                { ocl_array_retain(v.data.array_val); vm_push(vm, v); }
            else
                vm_push(vm, v);
            break;
        }

        case OP_STORE_VAR: {
            CallFrame *f = current_frame(vm);
            if (!f) {
                vm_error(vm, LOC, "OP_STORE_VAR: no active call frame");
                vm_pop_free(vm);
                break;
            }
            Value raw = vm_pop(vm);
            Value v = (raw.type == VALUE_STRING && !raw.owned)
                      ? value_string_copy(raw.data.string_val)
                      : raw;
            ensure_local(vm, f, ins.operand1);
            value_free(f->locals[ins.operand1]);
            f->locals[ins.operand1] = v;
            break;
        }

        case OP_LOAD_GLOBAL:
            ensure_global(vm, ins.operand1);
            {
                Value g = vm->globals[ins.operand1];
                if (g.type == VALUE_STRING)
                    vm_push(vm, value_string_borrow(g.data.string_val));
                else if (g.type == VALUE_ARRAY)
                    { ocl_array_retain(g.data.array_val); vm_push(vm, g); }
                else
                    vm_push(vm, g);
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
                vm_error(vm, LOC, "'+' cannot combine %s and %s",
                         value_type_name(a.type), value_type_name(b.type));
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            }
            break;
        }

        case OP_SUBTRACT: ARITH_OP(LOC, a - b, a - b); break;
        case OP_MULTIPLY: ARITH_OP(LOC, a * b, a * b); break;

        case OP_DIVIDE: {
            Value b = vm_pop(vm);
            Value a = vm_pop(vm);
            bool  div_by_zero = (b.type == VALUE_FLOAT) ? (b.data.float_val == 0.0)
                                                        : (b.data.int_val   == 0);
            if (div_by_zero) {
                vm_error(vm, LOC, "division by zero");
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
            if (a.type != VALUE_INT || b.type != VALUE_INT) {
                vm_error(vm, LOC, "'%%' requires Int operands, got %s and %s",
                         value_type_name(a.type), value_type_name(b.type));
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            } else if (b.data.int_val == 0) {
                vm_error(vm, LOC, "modulo by zero");
                value_free(a); value_free(b);
                vm_push(vm, value_null());
            } else {
                int64_t r = a.data.int_val % b.data.int_val;
                value_free(a); value_free(b);
                vm_push(vm, value_int(r));
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
                vm_error(vm, LOC, "unary '-' on %s", value_type_name(a.type));
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

        case OP_LESS:          CMP_OP(LOC, a < b,  a < b);  break;
        case OP_LESS_EQUAL:    CMP_OP(LOC, a <= b, a <= b); break;
        case OP_GREATER:       CMP_OP(LOC, a > b,  a > b);  break;
        case OP_GREATER_EQUAL: CMP_OP(LOC, a >= b, a >= b); break;

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

        case OP_CALL: {
            uint32_t fidx = ins.operand1;
            uint32_t argc = ins.operand2;

            if (fidx == 0xFFFFFFFF) {
                vm_error(vm, LOC, "call to unresolved function");
                discard_args_push_null(vm, (int)argc);
                break;
            }
            if (fidx >= (uint32_t)vm->bytecode->function_count) {
                vm_error(vm, LOC, "invalid function index %u (only %zu defined)",
                         fidx, vm->bytecode->function_count);
                discard_args_push_null(vm, (int)argc);
                break;
            }

            FuncEntry *fe = &vm->bytecode->functions[fidx];

            if ((int)argc != fe->param_count) {
                vm_error(vm, LOC,
                         "function '%s' expects %d argument(s), got %u",
                         fe->name ? fe->name : "?", fe->param_count, argc);
                discard_args_push_null(vm, (int)argc);
                break;
            }

            if (vm->frame_top >= VM_FRAMES_MAX) {
                vm_error(vm, LOC,
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

            int local_cap = (fe->local_count > (int)argc ? fe->local_count : (int)argc) + 8;
            frame->locals          = ocl_malloc((size_t)local_cap * sizeof(Value));
            frame->local_count     = (size_t)local_cap;
            frame->local_capacity  = (size_t)local_cap;
            for (int i = 0; i < local_cap; i++)
                frame->locals[i] = value_null();

            for (int i = (int)argc - 1; i >= 0; i--) {
                Value popped = vm_pop(vm);
                frame->locals[i] = (popped.type == VALUE_STRING && !popped.owned)
                                   ? value_string_copy(popped.data.string_val)
                                   : popped;
            }

            vm->pc = fe->start_ip;
            continue;
        }

        case OP_RETURN: {
            Value ret_raw = vm_pop(vm);

            if (vm->frame_top == 0) {
                if (ret_raw.type == VALUE_INT)
                    vm->exit_code = (int)ret_raw.data.int_val;
                value_free(ret_raw);
                vm->halted = true;
                break;
            }

            Value ret = (ret_raw.type == VALUE_STRING && !ret_raw.owned)
                        ? value_string_copy(ret_raw.data.string_val)
                        : ret_raw;

            CallFrame *frame = &vm->frames[--vm->frame_top];
            uint32_t   ret_ip = frame->return_ip;

            for (size_t i = 0; i < frame->local_count; i++)
                value_free(frame->locals[i]);
            ocl_free(frame->locals);

            while (vm->stack_top > frame->stack_base)
                vm_pop_free(vm);

            vm_push(vm, ret);
            vm->pc = ret_ip;
            continue;
        }

        case OP_HALT:
            vm->halted = true;
            if (vm->stack_top > 0) {
                Value top = vm_peek(vm, 0);
                if (top.type == VALUE_INT)
                    vm->exit_code = (int)top.data.int_val;
            }
            break;

        case OP_CALL_BUILTIN: {
            int bid  = (int)ins.operand1;
            int argc = (int)ins.operand2;
            switch (bid) {
                case BUILTIN_PRINT:  builtin_print(vm, argc);  break;
                case BUILTIN_PRINTF: builtin_printf(vm, argc); break;
                default:
                    if (!stdlib_dispatch(vm, bid, argc)) {
                        vm_error(vm, LOC, "unknown built-in function id %d", bid);
                        discard_args_push_null(vm, argc);
                    }
                    break;
            }
            break;
        }

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
                    vm_error(vm, LOC, "string index must be Int, got %s",
                             value_type_name(idx_v.type));
                    value_free(idx_v); value_free(arr_v);
                    vm_push(vm, value_null());
                    break;
                }
                const char *s    = arr_v.data.string_val ? arr_v.data.string_val : "";
                int64_t     idx  = idx_v.data.int_val;
                size_t      slen = strlen(s);
                if (idx < 0 || (size_t)idx >= slen) {
                    vm_error(vm, LOC,
                             "string index %"PRId64" out of bounds [0, %zu)",
                             idx, slen);
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
                vm_error(vm, LOC, "index access on non-indexable type %s",
                         value_type_name(arr_v.type));
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, value_null());
                break;
            }
            if (idx_v.type != VALUE_INT) {
                vm_error(vm, LOC, "array index must be Int, got %s",
                         value_type_name(idx_v.type));
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, value_null());
                break;
            }
            {
                int64_t   idx = idx_v.data.int_val;
                OclArray *arr = arr_v.data.array_val;
                if (idx < 0 || (size_t)idx >= arr->length) {
                    vm_error(vm, LOC,
                             "array index %"PRId64" out of bounds [0, %zu)",
                             idx, arr->length);
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
                vm_error(vm, LOC, "OP_ARRAY_SET on non-Array type %s",
                         value_type_name(arr_v.type));
                value_free(idx_v); value_free(arr_v); value_free(val);
                break;
            }
            if (idx_v.type != VALUE_INT) {
                vm_error(vm, LOC, "array index must be Int, got %s",
                         value_type_name(idx_v.type));
                value_free(idx_v); value_free(arr_v); value_free(val);
                break;
            }
            int64_t idx = idx_v.data.int_val;
            if (idx < 0) {
                vm_error(vm, LOC, "array index must be non-negative, got %"PRId64, idx);
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
                vm_error(vm, LOC, "arrayLen requires Array or String, got %s",
                         value_type_name(arr_v.type));
                len = 0;
            }
            value_free(arr_v);
            vm_push(vm, value_int(len));
            break;
        }

        default:
            vm_error(vm, LOC, "unknown opcode %d at ip=%u",
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