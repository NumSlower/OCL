/*
 * vm.c — OCL Bytecode Virtual Machine
 *
 * String memory model
 * ───────────────────
 * Every Value carries an `owned` flag (see common.h).  Only owned string
 * Values free their char* on value_free().  This lets us push *borrowed*
 * views of constant-pool strings (zero allocation per push) and only
 * allocate a fresh copy when the string is stored somewhere that must
 * outlive the current expression (OP_STORE_VAR, OP_STORE_GLOBAL, and
 * the argument arrays built in builtin_print / builtin_printf).
 *
 * Ownership rules enforced in this file:
 *   OP_PUSH_CONST  — pushes a borrowed view (no alloc)
 *   OP_LOAD_VAR    — pushes a borrowed view of the local slot
 *   OP_LOAD_GLOBAL — pushes a borrowed view of the global slot
 *   OP_STORE_VAR   — calls value_own_copy() before storing
 *   OP_STORE_GLOBAL— calls value_own_copy() before storing
 *   OP_ADD (str)   — allocates a new owned concatenation
 *   OP_CONCAT      — allocates a new owned concatenation
 *   OP_RETURN      — value_own_copy() called BEFORE freeing locals (UAF fix)
 *   OP_POP         — value_free() is a no-op for borrowed strings
 *   builtins       — pop args (borrow-safe via value_free), push owned results
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "ocl_stdlib.h"
#include "common.h"

/* ═══════════════════════════════════════════════════════════════════
   Lifecycle
═══════════════════════════════════════════════════════════════════ */

VM *vm_create(Bytecode *bytecode) {
    VM *vm           = ocl_malloc(sizeof(VM));
    vm->bytecode     = bytecode;
    vm->stack_top    = 0;
    vm->frame_top    = 0;
    vm->globals      = NULL;
    vm->global_count = 0;
    vm->global_capacity = 0;
    vm->pc           = 0;
    vm->halted       = false;
    vm->exit_code    = 0;
    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;
    /* Free any remaining values on the stack */
    for (size_t i = 0; i < vm->stack_top; i++)
        value_free(vm->stack[i]);
    /* Free call frames */
    for (size_t i = 0; i < vm->frame_top; i++) {
        for (size_t j = 0; j < vm->frames[i].local_count; j++)
            value_free(vm->frames[i].locals[j]);
        ocl_free(vm->frames[i].locals);
    }
    /* Free globals */
    for (size_t i = 0; i < vm->global_count; i++)
        value_free(vm->globals[i]);
    ocl_free(vm->globals);
    ocl_free(vm);
}

/* ═══════════════════════════════════════════════════════════════════
   Stack helpers
═══════════════════════════════════════════════════════════════════ */

void vm_push(VM *vm, Value v) {
    if (vm->stack_top >= VM_STACK_MAX) {
        fprintf(stderr, "RUNTIME ERROR: Stack overflow\n");
        value_free(v);
        vm->halted    = true;
        vm->exit_code = 1;
        return;
    }
    vm->stack[vm->stack_top++] = v;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top == 0) {
        fprintf(stderr, "RUNTIME ERROR: Stack underflow\n");
        vm->halted    = true;
        vm->exit_code = 1;
        return value_null();
    }
    return vm->stack[--vm->stack_top];
}

/*
 * vm_pop_free — pop a value and immediately free it.
 * For owned strings this frees the allocation; for borrowed strings
 * and all other types this is a no-op (value_free handles both cases).
 */
static void vm_pop_free(VM *vm) {
    value_free(vm_pop(vm));
}

Value vm_peek(VM *vm, size_t depth) {
    if (depth >= vm->stack_top) return value_null();
    return vm->stack[vm->stack_top - 1 - depth];
}

/* ═══════════════════════════════════════════════════════════════════
   Global variable helpers
═══════════════════════════════════════════════════════════════════ */

static void ensure_global(VM *vm, uint32_t idx) {
    if (idx >= vm->global_capacity) {
        uint32_t new_cap = idx + 16;
        vm->globals = ocl_realloc(vm->globals, new_cap * sizeof(Value));
        for (uint32_t i = (uint32_t)vm->global_capacity; i < new_cap; i++)
            vm->globals[i] = value_null();
        vm->global_capacity = new_cap;
    }
    if (idx >= (uint32_t)vm->global_count)
        vm->global_count = idx + 1;
}

/* ═══════════════════════════════════════════════════════════════════
   Frame helpers
═══════════════════════════════════════════════════════════════════ */

static CallFrame *current_frame(VM *vm) {
    return vm->frame_top > 0 ? &vm->frames[vm->frame_top - 1] : NULL;
}

static void ensure_local(VM *vm, CallFrame *f, uint32_t idx) {
    (void)vm;
    if (idx >= f->local_capacity) {
        uint32_t new_cap = idx + 16;
        f->locals = ocl_realloc(f->locals, new_cap * sizeof(Value));
        for (uint32_t i = (uint32_t)f->local_capacity; i < new_cap; i++)
            f->locals[i] = value_null();
        f->local_capacity = new_cap;
    }
    if (idx >= (uint32_t)f->local_count)
        f->local_count = idx + 1;
}

/* ═══════════════════════════════════════════════════════════════════
   Built-in helpers  (print / printf stay here for performance)
═══════════════════════════════════════════════════════════════════ */

/*
 * pop_args_for_builtin — collects argc values from the stack into a
 * temporary owned array.  Each popped value may be borrowed or owned;
 * we leave it as-is and let the callee call value_free() on each slot.
 */
static Value *pop_args_for_builtin(VM *vm, int argc) {
    if (argc <= 0) return NULL;
    Value *args = ocl_malloc((size_t)argc * sizeof(Value));
    for (int i = argc - 1; i >= 0; i--)
        args[i] = vm_pop(vm);
    return args;
}

static void builtin_print(VM *vm, int argc) {
    Value *args = pop_args_for_builtin(vm, argc);
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        switch (args[i].type) {
            case VALUE_INT:    printf("%ld",  (long)args[i].data.int_val);  break;
            case VALUE_FLOAT:  printf("%g",   args[i].data.float_val);      break;
            case VALUE_STRING: printf("%s",   args[i].data.string_val
                                             ? args[i].data.string_val : ""); break;
            case VALUE_BOOL:   printf("%s",   args[i].data.bool_val
                                             ? "true" : "false"); break;
            case VALUE_CHAR:   printf("%c",   args[i].data.char_val);       break;
            case VALUE_NULL:   printf("null");                               break;
            default: break;
        }
    }
    printf("\n");
    for (int i = 0; i < argc; i++) value_free(args[i]);
    ocl_free(args);
    vm_push(vm, value_null());
}

static void builtin_printf(VM *vm, int argc) {
    if (argc < 1) { vm_push(vm, value_null()); return; }

    Value *args = pop_args_for_builtin(vm, argc);

    if (args[0].type != VALUE_STRING) {
        printf("%s", value_to_string(args[0]));
        for (int i = 0; i < argc; i++) value_free(args[i]);
        ocl_free(args);
        vm_push(vm, value_null());
        return;
    }

    const char *fmt      = args[0].data.string_val;
    int         next_arg = 1;

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] == '\\' && fmt[i+1]) {
            i++;
            switch (fmt[i]) {
                case 'n':  putchar('\n'); break;
                case 't':  putchar('\t'); break;
                case 'r':  putchar('\r'); break;
                case '\\': putchar('\\'); break;
                default:   putchar(fmt[i]); break;
            }
        } else if (fmt[i] == '%' && fmt[i+1]) {
            i++;
            switch (fmt[i]) {
                case 'd': case 'i':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        if      (a.type == VALUE_INT)   printf("%ld", (long)a.data.int_val);
                        else if (a.type == VALUE_FLOAT) printf("%ld", (long)a.data.float_val);
                        else                            printf("%s",  value_to_string(a));
                    } break;
                case 'f':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        if      (a.type == VALUE_FLOAT) printf("%g",  a.data.float_val);
                        else if (a.type == VALUE_INT)   printf("%g",  (double)a.data.int_val);
                        else                            printf("%s",  value_to_string(a));
                    } break;
                case 's':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        printf("%s", value_to_string(a));
                    } break;
                case 'c':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        if (a.type == VALUE_CHAR) printf("%c", a.data.char_val);
                        else                      printf("%s", value_to_string(a));
                    } break;
                case 'b':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        printf("%s", value_is_truthy(a) ? "true" : "false");
                    } break;
                case '%':
                    putchar('%'); break;
                default:
                    putchar('%'); putchar(fmt[i]); break;
            }
        } else {
            putchar(fmt[i]);
        }
    }

    for (int i = 0; i < argc; i++) value_free(args[i]);
    ocl_free(args);
    vm_push(vm, value_null());
}

/* ═══════════════════════════════════════════════════════════════════
   Main execution loop
═══════════════════════════════════════════════════════════════════ */

int vm_execute(VM *vm) {
    if (!vm || !vm->bytecode) return 1;

#define ARITH_OP(OP_INT, OP_FLOAT) do {                                     \
    Value b = vm_pop(vm); Value a = vm_pop(vm);                             \
    Value _result;                                                           \
    if (a.type == VALUE_INT && b.type == VALUE_INT) {                       \
        _result = value_int(OP_INT);                                         \
    } else {                                                                 \
        double af = (a.type == VALUE_FLOAT) ? a.data.float_val              \
                                            : (double)a.data.int_val;       \
        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val              \
                                            : (double)b.data.int_val;       \
        _result = value_float(OP_FLOAT);                                     \
    }                                                                        \
    value_free(a); value_free(b);                                            \
    vm_push(vm, _result);                                                    \
} while(0)

#define CMP_OP(OP_INT, OP_FLOAT) do {                                       \
    Value b = vm_pop(vm); Value a = vm_pop(vm);                             \
    bool r;                                                                  \
    if (a.type == VALUE_INT && b.type == VALUE_INT) {                       \
        r = OP_INT;                                                          \
    } else {                                                                 \
        double af = (a.type==VALUE_FLOAT) ? a.data.float_val                \
                                          : (double)a.data.int_val;         \
        double bf = (b.type==VALUE_FLOAT) ? b.data.float_val                \
                                          : (double)b.data.int_val;         \
        r = OP_FLOAT;                                                        \
    }                                                                        \
    value_free(a); value_free(b);                                            \
    vm_push(vm, value_bool(r));                                              \
} while(0)

    while (!vm->halted && vm->pc < (uint32_t)vm->bytecode->instruction_count) {
        Instruction ins = vm->bytecode->instructions[vm->pc];

        switch (ins.opcode) {

            /* ── Stack ───────────────────────────────────────── */
            case OP_PUSH_CONST:
                if (ins.operand1 < (uint32_t)vm->bytecode->constant_count) {
                    Value c = vm->bytecode->constants[ins.operand1];
                    if (c.type == VALUE_STRING)
                        vm_push(vm, value_string_borrow(c.data.string_val));
                    else
                        vm_push(vm, c);
                } else {
                    vm_push(vm, value_null());
                }
                break;

            case OP_POP:
                vm_pop_free(vm);
                break;

            /* ── Variables ───────────────────────────────────── */
            case OP_LOAD_VAR: {
                CallFrame *f = current_frame(vm);
                if (f && ins.operand1 < (uint32_t)f->local_count) {
                    Value v = f->locals[ins.operand1];
                    if (v.type == VALUE_STRING)
                        vm_push(vm, value_string_borrow(v.data.string_val));
                    else
                        vm_push(vm, v);
                } else {
                    vm_push(vm, value_null());
                }
                break;
            }
            case OP_STORE_VAR: {
                CallFrame *f = current_frame(vm);
                if (f) {
                    Value v = value_own_copy(vm_pop(vm));
                    ensure_local(vm, f, ins.operand1);
                    value_free(f->locals[ins.operand1]);
                    f->locals[ins.operand1] = v;
                } else {
                    vm_pop_free(vm);
                }
                break;
            }
            case OP_LOAD_GLOBAL:
                ensure_global(vm, ins.operand1);
                {
                    Value g = vm->globals[ins.operand1];
                    if (g.type == VALUE_STRING)
                        vm_push(vm, value_string_borrow(g.data.string_val));
                    else
                        vm_push(vm, g);
                }
                break;

            case OP_STORE_GLOBAL: {
                Value v = value_own_copy(vm_pop(vm));
                ensure_global(vm, ins.operand1);
                value_free(vm->globals[ins.operand1]);
                vm->globals[ins.operand1] = v;
                break;
            }

            /* ── Arithmetic ──────────────────────────────────── */
            case OP_ADD: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                if (a.type == VALUE_STRING && b.type == VALUE_STRING) {
                    const char *as = a.data.string_val ? a.data.string_val : "";
                    const char *bs = b.data.string_val ? b.data.string_val : "";
                    size_t len = strlen(as) + strlen(bs);
                    char  *s   = ocl_malloc(len + 1);
                    strcpy(s, as); strcat(s, bs);
                    value_free(a); value_free(b);
                    vm_push(vm, value_string(s));
                } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                    int64_t result = a.data.int_val + b.data.int_val;
                    value_free(a); value_free(b);
                    vm_push(vm, value_int(result));
                } else {
                    double af = (a.type == VALUE_FLOAT) ? a.data.float_val
                                                        : (double)a.data.int_val;
                    double bf = (b.type == VALUE_FLOAT) ? b.data.float_val
                                                        : (double)b.data.int_val;
                    value_free(a); value_free(b);
                    vm_push(vm, value_float(af + bf));
                }
                break;
            }
            case OP_SUBTRACT: ARITH_OP(a.data.int_val - b.data.int_val, af - bf); break;
            case OP_MULTIPLY: ARITH_OP(a.data.int_val * b.data.int_val, af * bf); break;
            case OP_DIVIDE: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                bool bz = (b.type == VALUE_FLOAT) ? (b.data.float_val == 0.0)
                                                  : (b.data.int_val   == 0);
                if (bz) {
                    fprintf(stderr, "RUNTIME ERROR: Division by zero [%d:%d]\n",
                            ins.location.line, ins.location.column);
                    value_free(a); value_free(b);
                    vm_push(vm, value_null());
                } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                    int64_t result = a.data.int_val / b.data.int_val;
                    value_free(a); value_free(b);
                    vm_push(vm, value_int(result));
                } else {
                    double af = (a.type == VALUE_FLOAT) ? a.data.float_val
                                                        : (double)a.data.int_val;
                    double bf = (b.type == VALUE_FLOAT) ? b.data.float_val
                                                        : (double)b.data.int_val;
                    value_free(a); value_free(b);
                    vm_push(vm, value_float(af / bf));
                }
                break;
            }
            case OP_MODULO: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                Value result;
                if (a.type == VALUE_INT && b.type == VALUE_INT && b.data.int_val != 0)
                    result = value_int(a.data.int_val % b.data.int_val);
                else
                    result = value_null();
                value_free(a); value_free(b);
                vm_push(vm, result);
                break;
            }
            case OP_NEGATE: {
                Value a = vm_pop(vm);
                Value result;
                if      (a.type == VALUE_INT)   result = value_int(-a.data.int_val);
                else if (a.type == VALUE_FLOAT) result = value_float(-a.data.float_val);
                else                            result = value_null();
                value_free(a);
                vm_push(vm, result);
                break;
            }
            case OP_NOT: {
                Value a = vm_pop(vm);
                bool r = !value_is_truthy(a);
                value_free(a);
                vm_push(vm, value_bool(r));
                break;
            }

            /* ── Comparison ──────────────────────────────────── */
            case OP_EQUAL: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                bool r = false;
                if (a.type == b.type) {
                    switch (a.type) {
                        case VALUE_INT:    r = a.data.int_val   == b.data.int_val;   break;
                        case VALUE_FLOAT:  r = a.data.float_val == b.data.float_val; break;
                        case VALUE_BOOL:   r = a.data.bool_val  == b.data.bool_val;  break;
                        case VALUE_CHAR:   r = a.data.char_val  == b.data.char_val;  break;
                        case VALUE_STRING:
                            r = (a.data.string_val && b.data.string_val)
                                ? !strcmp(a.data.string_val, b.data.string_val)
                                : (a.data.string_val == b.data.string_val);
                            break;
                        case VALUE_NULL: r = true; break;
                        default: break;
                    }
                }
                value_free(a); value_free(b);
                vm_push(vm, value_bool(r));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                bool r = true;
                if (a.type == b.type) {
                    switch (a.type) {
                        case VALUE_INT:   r = a.data.int_val   != b.data.int_val;   break;
                        case VALUE_FLOAT: r = a.data.float_val != b.data.float_val; break;
                        case VALUE_BOOL:  r = a.data.bool_val  != b.data.bool_val;  break;
                        case VALUE_CHAR:  r = a.data.char_val  != b.data.char_val;  break;
                        case VALUE_STRING:
                            r = (a.data.string_val && b.data.string_val)
                                ? !!strcmp(a.data.string_val, b.data.string_val)
                                : (a.data.string_val != b.data.string_val);
                            break;
                        case VALUE_NULL:  r = false; break;
                        default: break;
                    }
                }
                value_free(a); value_free(b);
                vm_push(vm, value_bool(r));
                break;
            }
            case OP_LESS:          CMP_OP(a.data.int_val <  b.data.int_val, af <  bf); break;
            case OP_LESS_EQUAL:    CMP_OP(a.data.int_val <= b.data.int_val, af <= bf); break;
            case OP_GREATER:       CMP_OP(a.data.int_val >  b.data.int_val, af >  bf); break;
            case OP_GREATER_EQUAL: CMP_OP(a.data.int_val >= b.data.int_val, af >= bf); break;

            /* ── Logical ─────────────────────────────────────── */
            case OP_AND: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                bool r = value_is_truthy(a) && value_is_truthy(b);
                value_free(a); value_free(b);
                vm_push(vm, value_bool(r));
                break;
            }
            case OP_OR: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                bool r = value_is_truthy(a) || value_is_truthy(b);
                value_free(a); value_free(b);
                vm_push(vm, value_bool(r));
                break;
            }

            /* ── Control flow ────────────────────────────────── */
            case OP_JUMP:
                vm->pc = ins.operand1;
                continue;

            case OP_JUMP_IF_FALSE: {
                Value cond = vm_pop(vm);
                bool taken = !value_is_truthy(cond);
                value_free(cond);
                if (taken) { vm->pc = ins.operand1; continue; }
                break;
            }
            case OP_JUMP_IF_TRUE: {
                Value cond = vm_pop(vm);
                bool taken = value_is_truthy(cond);
                value_free(cond);
                if (taken) { vm->pc = ins.operand1; continue; }
                break;
            }

            /* ── Function call ───────────────────────────────── */
            case OP_CALL: {
                uint32_t fidx = ins.operand1;
                uint32_t argc = ins.operand2;

                if (fidx >= (uint32_t)vm->bytecode->function_count) {
                    fprintf(stderr, "RUNTIME ERROR: Invalid function index %u\n", fidx);
                    vm->halted = true; vm->exit_code = 1;
                    break;
                }
                FuncEntry *fe = &vm->bytecode->functions[fidx];

                if (vm->frame_top >= VM_FRAMES_MAX) {
                    fprintf(stderr, "RUNTIME ERROR: Call stack overflow\n");
                    vm->halted = true; vm->exit_code = 1;
                    break;
                }

                CallFrame *frame  = &vm->frames[vm->frame_top++];
                frame->return_ip  = vm->pc + 1;
                frame->stack_base = (uint32_t)vm->stack_top;

                int local_cap = fe->local_count > (int)argc
                                ? fe->local_count + 8 : (int)argc + 8;
                frame->locals         = ocl_malloc((size_t)local_cap * sizeof(Value));
                frame->local_count    = (size_t)local_cap;
                frame->local_capacity = (size_t)local_cap;
                for (int i = 0; i < local_cap; i++) frame->locals[i] = value_null();

                /* Arguments must be owned before entering the new frame */
                for (int i = (int)argc - 1; i >= 0; i--) {
                    if (vm->stack_top > 0)
                        frame->locals[i] = value_own_copy(vm_pop(vm));
                }

                vm->pc = fe->start_ip;
                continue;
            }

            case OP_RETURN: {
                /*
                 * ┌─────────────────────────────────────────────────────┐
                 * │  FIX: value_own_copy() MUST happen BEFORE we free   │
                 * │  the locals array.                                   │
                 * │                                                      │
                 * │  OP_LOAD_VAR pushes a *borrowed* pointer into a     │
                 * │  local slot.  If that borrowed value is the return   │
                 * │  expression, then:                                   │
                 * │    ret_val.data.string_val → points into locals[]    │
                 * │  Freeing locals[] first made that pointer dangling,  │
                 * │  and the subsequent value_own_copy() was a UAF.      │
                 * │                                                      │
                 * │  Correct order:                                      │
                 * │    1. pop return value from stack                    │
                 * │    2. value_own_copy() — copy while locals still live│
                 * │    3. free locals                                    │
                 * │    4. discard leftover stack values                  │
                 * │    5. push the (now-safe) owned return value         │
                 * └─────────────────────────────────────────────────────┘
                 */
                Value ret_raw = vm_pop(vm);

                if (vm->frame_top == 0) {
                    /* Returning from the top level — just record exit code */
                    if (ret_raw.type == VALUE_INT)
                        vm->exit_code = (int)ret_raw.data.int_val;
                    value_free(ret_raw);
                    vm->halted = true;
                    break;
                }

                /*
                 * Step 2: copy NOW, before anything is freed.
                 * For a borrowed string this allocates a fresh heap copy.
                 * For an owned string or non-string this is a cheap no-op.
                 */
                Value ret_owned = value_own_copy(ret_raw);

                CallFrame *frame  = &vm->frames[--vm->frame_top];
                uint32_t   ret_ip = frame->return_ip;

                /* Step 3: free all locals (owned strings freed, borrowed skipped) */
                for (size_t i = 0; i < frame->local_count; i++)
                    value_free(frame->locals[i]);
                ocl_free(frame->locals);

                /* Step 4: discard any leftover stack values from this frame */
                while (vm->stack_top > frame->stack_base)
                    vm_pop_free(vm);

                /* Step 5: push the already-owned return value */
                vm_push(vm, ret_owned);

                vm->pc = ret_ip;
                continue;
            }

            case OP_HALT:
                vm->halted = true;
                if (vm->stack_top > 0) {
                    Value top = vm_peek(vm, 0);
                    if      (top.type == VALUE_INT)   vm->exit_code = (int)top.data.int_val;
                    else if (top.type == VALUE_BOOL)  vm->exit_code = top.data.bool_val ? 1 : 0;
                    else if (top.type == VALUE_FLOAT) vm->exit_code = (int)top.data.float_val;
                }
                break;

            /* ── Built-in dispatch ───────────────────────────── */
            case OP_CALL_BUILTIN: {
                int bid  = (int)ins.operand1;
                int argc = (int)ins.operand2;

                switch (bid) {
                    case BUILTIN_PRINT:  builtin_print(vm, argc);  break;
                    case BUILTIN_PRINTF: builtin_printf(vm, argc); break;
                    default:
                        if (!stdlib_dispatch(vm, bid, argc)) {
                            fprintf(stderr, "RUNTIME ERROR: Unknown built-in id %d\n", bid);
                            for (int i = 0; i < argc; i++) vm_pop_free(vm);
                            vm_push(vm, value_null());
                        }
                        break;
                }
                break;
            }

            /* ── Type conversions ────────────────────────────── */
            case OP_TO_INT: {
                Value a = vm_pop(vm);
                int64_t result;
                switch (a.type) {
                    case VALUE_INT:    result = a.data.int_val; break;
                    case VALUE_FLOAT:  result = (int64_t)a.data.float_val; break;
                    case VALUE_BOOL:   result = a.data.bool_val ? 1 : 0; break;
                    case VALUE_STRING: result = a.data.string_val
                                          ? (int64_t)strtoll(a.data.string_val, NULL, 10) : 0; break;
                    default:           result = 0; break;
                }
                value_free(a);
                vm_push(vm, value_int(result));
                break;
            }
            case OP_TO_FLOAT: {
                Value a = vm_pop(vm);
                double result;
                switch (a.type) {
                    case VALUE_FLOAT: result = a.data.float_val; break;
                    case VALUE_INT:   result = (double)a.data.int_val; break;
                    case VALUE_BOOL:  result = a.data.bool_val ? 1.0 : 0.0; break;
                    default:          result = 0.0; break;
                }
                value_free(a);
                vm_push(vm, value_float(result));
                break;
            }
            case OP_TO_STRING: {
                Value a = vm_pop(vm);
                Value s = value_string_copy(value_to_string(a));
                value_free(a);
                vm_push(vm, s);
                break;
            }
            case OP_CONCAT: {
                Value b = vm_pop(vm); Value a = vm_pop(vm);
                const char *as = value_to_string(a);
                const char *bs = value_to_string(b);
                size_t len = strlen(as) + strlen(bs);
                char  *s   = ocl_malloc(len + 1);
                strcpy(s, as); strcat(s, bs);
                value_free(a); value_free(b);
                vm_push(vm, value_string(s));
                break;
            }

            /* ── Array operations ────────────────────────────── */
            case OP_ARRAY_NEW:
            case OP_ARRAY_GET:
            case OP_ARRAY_SET:
            case OP_ARRAY_LEN:
                fprintf(stderr, "RUNTIME ERROR: Array operations not yet implemented\n");
                vm_push(vm, value_null());
                break;

            default:
                fprintf(stderr, "RUNTIME ERROR: Unknown opcode %d at ip=%u\n",
                        ins.opcode, vm->pc);
                vm->halted    = true;
                vm->exit_code = 1;
                break;
        }

        vm->pc++;
    }

    return vm->exit_code;

#undef ARITH_OP
#undef CMP_OP
}

Value vm_get_result(VM *vm) {
    if (!vm || vm->stack_top == 0) return value_null();
    return vm->stack[vm->stack_top - 1];
}