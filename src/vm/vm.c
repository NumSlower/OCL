#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "ocl_stdlib.h"
#include "common.h"

VM *vm_create(Bytecode *bytecode) {
    VM *vm        = ocl_malloc(sizeof(VM));
    vm->bytecode  = bytecode;
    vm->stack_top = 0;
    vm->frame_top = 0;
    vm->frame_capacity  = VM_FRAMES_INITIAL;
    vm->frames          = ocl_malloc(vm->frame_capacity * sizeof(CallFrame));
    vm->globals   = NULL;
    vm->global_count    = 0;
    vm->global_capacity = 0;
    vm->pc        = 0;
    vm->halted    = false;
    vm->exit_code = 0;
    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;
    for (size_t i = 0; i < vm->stack_top; i++) value_free(vm->stack[i]);
    for (size_t i = 0; i < vm->frame_top; i++) {
        for (size_t j = 0; j < vm->frames[i].local_count; j++)
            value_free(vm->frames[i].locals[j]);
        ocl_free(vm->frames[i].locals);
    }
    ocl_free(vm->frames);   /* <-- this line was missing */
    for (size_t i = 0; i < vm->global_count; i++) value_free(vm->globals[i]);
    ocl_free(vm->globals);
    ocl_free(vm);
}

static void vm_runtime_error(VM *vm, SourceLocation loc, const char *fmt, ...) {
    va_list ap;
    if (loc.filename && loc.line > 0)
        fprintf(stderr, "RUNTIME ERROR [%s:%d:%d]: ", loc.filename, loc.line, loc.column);
    else
        fprintf(stderr, "RUNTIME ERROR: ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
    vm->halted = true; vm->exit_code = 1;
}

void vm_push(VM *vm, Value v) {
    if (vm->stack_top >= VM_STACK_MAX) {
        fprintf(stderr, "RUNTIME ERROR: Stack overflow (depth=%zu)\n", vm->stack_top);
        value_free(v); vm->halted = true; vm->exit_code = 1; return;
    }
    vm->stack[vm->stack_top++] = v;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top == 0) {
        fprintf(stderr, "RUNTIME ERROR: Stack underflow at ip=%u\n", vm->pc);
        vm->halted = true; vm->exit_code = 1; return value_null();
    }
    return vm->stack[--vm->stack_top];
}

static void vm_pop_free(VM *vm) { value_free(vm_pop(vm)); }

Value vm_peek(VM *vm, size_t depth) {
    if (depth >= vm->stack_top) return value_null();
    return vm->stack[vm->stack_top - 1 - depth];
}

static void ensure_global(VM *vm, uint32_t idx) {
    if (idx >= vm->global_capacity) {
        uint32_t new_cap = idx + 16;
        vm->globals = ocl_realloc(vm->globals, new_cap * sizeof(Value));
        for (uint32_t i = (uint32_t)vm->global_capacity; i < new_cap; i++) vm->globals[i] = value_null();
        vm->global_capacity = new_cap;
    }
    if (idx >= (uint32_t)vm->global_count) vm->global_count = idx + 1;
}

static CallFrame *current_frame(VM *vm) {
    return vm->frame_top > 0 ? &vm->frames[vm->frame_top - 1] : NULL;
}

static void ensure_local(VM *vm, CallFrame *f, uint32_t idx) {
    (void)vm;
    if (idx >= f->local_capacity) {
        uint32_t new_cap = idx + 16;
        f->locals = ocl_realloc(f->locals, new_cap * sizeof(Value));
        for (uint32_t i = (uint32_t)f->local_capacity; i < new_cap; i++) f->locals[i] = value_null();
        f->local_capacity = new_cap;
    }
    if (idx >= (uint32_t)f->local_count) f->local_count = idx + 1;
}

static Value *pop_args_for_builtin(VM *vm, int argc) {
    if (argc <= 0) return NULL;
    Value *args = ocl_malloc((size_t)argc * sizeof(Value));
    for (int i = argc - 1; i >= 0; i--) args[i] = vm_pop(vm);
    return args;
}

static void builtin_print(VM *vm, int argc) {
    Value *args = pop_args_for_builtin(vm, argc);
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        switch (args[i].type) {
            case VALUE_INT:    printf("%ld",  (long)args[i].data.int_val);  break;
            case VALUE_FLOAT:  printf("%g",   args[i].data.float_val);      break;
            case VALUE_STRING: printf("%s",   args[i].data.string_val ? args[i].data.string_val : ""); break;
            case VALUE_BOOL:   printf("%s",   args[i].data.bool_val ? "true" : "false"); break;
            case VALUE_CHAR:   printf("%c",   args[i].data.char_val);       break;
            case VALUE_NULL:   printf("null");                               break;
            case VALUE_ARRAY:  {
                printf("%s", value_to_string(args[i]));
                break;
            }
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
        ocl_free(args); vm_push(vm, value_null()); return;
    }
    const char *fmt = args[0].data.string_val;
    int next_arg = 1;
    for (size_t i = 0; fmt && fmt[i]; i++) {
        if (fmt[i] == '\\' && fmt[i+1]) {
            i++;
            switch (fmt[i]) {
                case 'n': putchar('\n'); break; case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break; case '\\': putchar('\\'); break;
                default:  putchar(fmt[i]); break;
            }
        } else if (fmt[i] == '%' && fmt[i+1]) {
            i++;
            switch (fmt[i]) {
                case 'd': case 'i':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        if (a.type == VALUE_INT) printf("%ld", (long)a.data.int_val);
                        else if (a.type == VALUE_FLOAT) printf("%ld", (long)a.data.float_val);
                        else { printf("%s", value_to_string(a)); }
                    } break;
                case 'f':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        if (a.type == VALUE_FLOAT) printf("%g", a.data.float_val);
                        else if (a.type == VALUE_INT) printf("%g", (double)a.data.int_val);
                        else { printf("%s", value_to_string(a)); }
                    } break;
                case 's':
                    if (next_arg < argc) {
                        printf("%s", value_to_string(args[next_arg++]));
                    } break;
                case 'c':
                    if (next_arg < argc) {
                        Value a = args[next_arg++];
                        if (a.type == VALUE_CHAR) printf("%c", a.data.char_val);
                        else { printf("%s", value_to_string(a)); }
                    } break;
                case 'b':
                    if (next_arg < argc) { printf("%s", value_is_truthy(args[next_arg++]) ? "true" : "false"); } break;
                case '%': putchar('%'); break;
                default:  putchar('%'); putchar(fmt[i]); break;
            }
        } else {
            putchar(fmt[i]);
        }
    }
    for (int i = 0; i < argc; i++) value_free(args[i]);
    ocl_free(args);
    vm_push(vm, value_null());
}

int vm_execute(VM *vm) {
    if (!vm || !vm->bytecode) return 1;

#define LOC (ins.location)

#define ARITH_OP(OP_INT, OP_FLOAT) do { \
    Value b = vm_pop(vm); Value a = vm_pop(vm); Value _result; \
    if (a.type == VALUE_INT && b.type == VALUE_INT) { _result = value_int(OP_INT); } \
    else if ((a.type==VALUE_INT||a.type==VALUE_FLOAT)&&(b.type==VALUE_INT||b.type==VALUE_FLOAT)) { \
        double af=(a.type==VALUE_FLOAT)?a.data.float_val:(double)a.data.int_val; \
        double bf=(b.type==VALUE_FLOAT)?b.data.float_val:(double)b.data.int_val; \
        _result=value_float(OP_FLOAT); \
    } else { \
        vm_runtime_error(vm,LOC,"Arithmetic on non-numeric types (%s, %s)",value_type_name(a.type),value_type_name(b.type)); \
        _result=value_null(); \
    } \
    value_free(a); value_free(b); vm_push(vm, _result); \
} while(0)

#define CMP_OP(OP_INT, OP_FLOAT) do { \
    Value b = vm_pop(vm); Value a = vm_pop(vm); bool r; \
    if (a.type==VALUE_INT&&b.type==VALUE_INT) { r=OP_INT; } \
    else if ((a.type==VALUE_INT||a.type==VALUE_FLOAT)&&(b.type==VALUE_INT||b.type==VALUE_FLOAT)) { \
        double af=(a.type==VALUE_FLOAT)?a.data.float_val:(double)a.data.int_val; \
        double bf=(b.type==VALUE_FLOAT)?b.data.float_val:(double)b.data.int_val; \
        r=OP_FLOAT; \
    } else { r=false; } \
    value_free(a); value_free(b); vm_push(vm, value_bool(r)); \
} while(0)

    while (!vm->halted && vm->pc < (uint32_t)vm->bytecode->instruction_count) {
        Instruction ins = vm->bytecode->instructions[vm->pc];

        switch (ins.opcode) {

        case OP_PUSH_CONST:
            if (ins.operand1 < (uint32_t)vm->bytecode->constant_count) {
                Value c = vm->bytecode->constants[ins.operand1];
                if (c.type == VALUE_STRING)
                    vm_push(vm, value_string_borrow(c.data.string_val));
                else
                    vm_push(vm, c);
            } else {
                vm_runtime_error(vm, LOC, "Invalid constant index %u", ins.operand1);
            }
            break;

        case OP_POP: vm_pop_free(vm); break;

        case OP_LOAD_VAR: {
            CallFrame *f = current_frame(vm);
            if (f && ins.operand1 < (uint32_t)f->local_count) {
                Value v = f->locals[ins.operand1];
                if (v.type == VALUE_STRING) vm_push(vm, value_string_borrow(v.data.string_val));
                else if (v.type == VALUE_ARRAY) { ocl_array_retain(v.data.array_val); vm_push(vm, v); }
                else vm_push(vm, v);
            } else {
                vm_runtime_error(vm, LOC, "OP_LOAD_VAR: slot %u out of bounds", ins.operand1);
            }
            break;
        }
        case OP_STORE_VAR: {
            CallFrame *f = current_frame(vm);
            if (f) {
                Value raw = vm_pop(vm);
                Value v = (raw.type == VALUE_STRING && !raw.owned)
                          ? value_string_copy(raw.data.string_val)
                          : raw;
                ensure_local(vm, f, ins.operand1);
                value_free(f->locals[ins.operand1]);
                f->locals[ins.operand1] = v;
            } else { vm_pop_free(vm); vm_runtime_error(vm, LOC, "OP_STORE_VAR: no active call frame"); }
            break;
        }
        case OP_LOAD_GLOBAL:
            ensure_global(vm, ins.operand1);
            {
                Value g = vm->globals[ins.operand1];
                if (g.type == VALUE_STRING) vm_push(vm, value_string_borrow(g.data.string_val));
                else if (g.type == VALUE_ARRAY) { ocl_array_retain(g.data.array_val); vm_push(vm, g); }
                else vm_push(vm, g);
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
            Value b = vm_pop(vm); Value a = vm_pop(vm);
            if (a.type == VALUE_STRING && b.type == VALUE_STRING) {
                const char *as = a.data.string_val ? a.data.string_val : "";
                const char *bs = b.data.string_val ? b.data.string_val : "";
                size_t len = strlen(as) + strlen(bs);
                char *s = ocl_malloc(len + 1); strcpy(s, as); strcat(s, bs);
                value_free(a); value_free(b); vm_push(vm, value_string(s));
            } else if (a.type == VALUE_STRING && b.type == VALUE_CHAR) {
                const char *as = a.data.string_val ? a.data.string_val : "";
                size_t len = strlen(as);
                char *s = ocl_malloc(len + 2);
                memcpy(s, as, len);
                s[len]   = b.data.char_val;
                s[len+1] = '\0';
                value_free(a); value_free(b); vm_push(vm, value_string(s));
            } else if (a.type == VALUE_CHAR && b.type == VALUE_STRING) {
                const char *bs = b.data.string_val ? b.data.string_val : "";
                size_t len = strlen(bs);
                char *s = ocl_malloc(len + 2);
                s[0] = a.data.char_val;
                memcpy(s + 1, bs, len + 1);
                value_free(a); value_free(b); vm_push(vm, value_string(s));
            } else if (a.type == VALUE_INT && b.type == VALUE_INT) {
                int64_t r = a.data.int_val + b.data.int_val; value_free(a); value_free(b); vm_push(vm, value_int(r));
            } else if ((a.type==VALUE_INT||a.type==VALUE_FLOAT)&&(b.type==VALUE_INT||b.type==VALUE_FLOAT)) {
                double af=(a.type==VALUE_FLOAT)?a.data.float_val:(double)a.data.int_val;
                double bf=(b.type==VALUE_FLOAT)?b.data.float_val:(double)b.data.int_val;
                value_free(a); value_free(b); vm_push(vm, value_float(af+bf));
            } else {
                vm_runtime_error(vm,LOC,"'+' used with %s and %s",value_type_name(a.type),value_type_name(b.type));
                value_free(a); value_free(b); vm_push(vm, value_null());
            }
            break;
        }
        case OP_SUBTRACT: ARITH_OP(a.data.int_val - b.data.int_val, af - bf); break;
        case OP_MULTIPLY: ARITH_OP(a.data.int_val * b.data.int_val, af * bf); break;
        case OP_DIVIDE: {
            Value b = vm_pop(vm); Value a = vm_pop(vm);
            bool bz = (b.type==VALUE_FLOAT) ? (b.data.float_val==0.0) : (b.data.int_val==0);
            if (bz) {
                vm_runtime_error(vm, LOC, "Division by zero");
                value_free(a); value_free(b); vm_push(vm, value_null());
            } else if (a.type==VALUE_INT&&b.type==VALUE_INT) {
                int64_t r=a.data.int_val/b.data.int_val; value_free(a); value_free(b); vm_push(vm,value_int(r));
            } else {
                double af=(a.type==VALUE_FLOAT)?a.data.float_val:(double)a.data.int_val;
                double bf=(b.type==VALUE_FLOAT)?b.data.float_val:(double)b.data.int_val;
                value_free(a); value_free(b); vm_push(vm,value_float(af/bf));
            }
            break;
        }
        case OP_MODULO: {
            Value b = vm_pop(vm); Value a = vm_pop(vm);
            if (a.type==VALUE_INT&&b.type==VALUE_INT) {
                if (b.data.int_val==0) { vm_runtime_error(vm,LOC,"Modulo by zero"); value_free(a); value_free(b); vm_push(vm,value_null()); }
                else { int64_t r=a.data.int_val%b.data.int_val; value_free(a); value_free(b); vm_push(vm,value_int(r)); }
            } else {
                vm_runtime_error(vm,LOC,"'%%' requires Int operands");
                value_free(a); value_free(b); vm_push(vm,value_null());
            }
            break;
        }
        case OP_NEGATE: {
            Value a = vm_pop(vm);
            if (a.type==VALUE_INT) { Value r=value_int(-a.data.int_val); value_free(a); vm_push(vm,r); }
            else if (a.type==VALUE_FLOAT) { Value r=value_float(-a.data.float_val); value_free(a); vm_push(vm,r); }
            else { vm_runtime_error(vm,LOC,"Unary '-' on non-numeric type"); value_free(a); vm_push(vm,value_null()); }
            break;
        }
        case OP_NOT: { Value a=vm_pop(vm); bool r=!value_is_truthy(a); value_free(a); vm_push(vm,value_bool(r)); break; }

        case OP_EQUAL: {
            Value b=vm_pop(vm); Value a=vm_pop(vm); bool r=false;
            if (a.type==b.type) {
                switch(a.type) {
                    case VALUE_INT:    r=a.data.int_val==b.data.int_val; break;
                    case VALUE_FLOAT:  r=a.data.float_val==b.data.float_val; break;
                    case VALUE_BOOL:   r=a.data.bool_val==b.data.bool_val; break;
                    case VALUE_CHAR:   r=a.data.char_val==b.data.char_val; break;
                    case VALUE_STRING: r=(a.data.string_val&&b.data.string_val)?!strcmp(a.data.string_val,b.data.string_val):(a.data.string_val==b.data.string_val); break;
                    case VALUE_NULL:   r=true; break;
                    default: break;
                }
            } else if ((a.type==VALUE_INT||a.type==VALUE_FLOAT)&&(b.type==VALUE_INT||b.type==VALUE_FLOAT)) {
                double af=(a.type==VALUE_FLOAT)?a.data.float_val:(double)a.data.int_val;
                double bf=(b.type==VALUE_FLOAT)?b.data.float_val:(double)b.data.int_val;
                r=(af==bf);
            }
            value_free(a); value_free(b); vm_push(vm,value_bool(r)); break;
        }
        case OP_NOT_EQUAL: {
            Value b=vm_pop(vm); Value a=vm_pop(vm); bool r=true;
            if (a.type==b.type) {
                switch(a.type) {
                    case VALUE_INT:    r=a.data.int_val!=b.data.int_val; break;
                    case VALUE_FLOAT:  r=a.data.float_val!=b.data.float_val; break;
                    case VALUE_BOOL:   r=a.data.bool_val!=b.data.bool_val; break;
                    case VALUE_CHAR:   r=a.data.char_val!=b.data.char_val; break;
                    case VALUE_STRING: r=(a.data.string_val&&b.data.string_val)?!!strcmp(a.data.string_val,b.data.string_val):(a.data.string_val!=b.data.string_val); break;
                    case VALUE_NULL:   r=false; break;
                    default: break;
                }
            } else if ((a.type==VALUE_INT||a.type==VALUE_FLOAT)&&(b.type==VALUE_INT||b.type==VALUE_FLOAT)) {
                double af=(a.type==VALUE_FLOAT)?a.data.float_val:(double)a.data.int_val;
                double bf=(b.type==VALUE_FLOAT)?b.data.float_val:(double)b.data.int_val;
                r=(af!=bf);
            }
            value_free(a); value_free(b); vm_push(vm,value_bool(r)); break;
        }
        case OP_LESS:          CMP_OP(a.data.int_val <  b.data.int_val, af <  bf); break;
        case OP_LESS_EQUAL:    CMP_OP(a.data.int_val <= b.data.int_val, af <= bf); break;
        case OP_GREATER:       CMP_OP(a.data.int_val >  b.data.int_val, af >  bf); break;
        case OP_GREATER_EQUAL: CMP_OP(a.data.int_val >= b.data.int_val, af >= bf); break;

        /*
         * OP_AND / OP_OR — fallback handlers for any bytecode that still
         * uses these opcodes directly (e.g. pre-compiled caches).
         * The code generator no longer emits these for && / || expressions;
         * it uses OP_JUMP_IF_FALSE / OP_JUMP_IF_TRUE sequences instead so
         * that both operators short-circuit correctly.
         */
        case OP_AND: {
            Value b=vm_pop(vm); Value a=vm_pop(vm);
            bool r=value_is_truthy(a)&&value_is_truthy(b);
            value_free(a); value_free(b); vm_push(vm,value_bool(r)); break;
        }
        case OP_OR: {
            Value b=vm_pop(vm); Value a=vm_pop(vm);
            bool r=value_is_truthy(a)||value_is_truthy(b);
            value_free(a); value_free(b); vm_push(vm,value_bool(r)); break;
        }

        case OP_JUMP:  vm->pc=ins.operand1; continue;
        case OP_JUMP_IF_FALSE: {
            Value cond=vm_pop(vm); bool taken=!value_is_truthy(cond); value_free(cond);
            if (taken) { vm->pc=ins.operand1; continue; }
            break;
        }
        case OP_JUMP_IF_TRUE: {
            Value cond=vm_pop(vm); bool taken=value_is_truthy(cond); value_free(cond);
            if (taken) { vm->pc=ins.operand1; continue; }
            break;
        }

        case OP_CALL: {
            uint32_t fidx=ins.operand1, argc=ins.operand2;
            if (fidx==0xFFFFFFFF) { vm_runtime_error(vm,LOC,"Call to unresolved function"); for(uint32_t i=0;i<argc;i++) vm_pop_free(vm); vm_push(vm,value_null()); break; }
            if (fidx>=(uint32_t)vm->bytecode->function_count) { vm_runtime_error(vm,LOC,"Invalid function index %u",fidx); for(uint32_t i=0;i<argc;i++) vm_pop_free(vm); vm_push(vm,value_null()); break; }
            FuncEntry *fe=&vm->bytecode->functions[fidx];
            if (vm->frame_top >= vm->frame_capacity) {
                size_t new_cap = vm->frame_capacity * VM_FRAMES_GROW;
                vm->frames = ocl_realloc(vm->frames, new_cap * sizeof(CallFrame));
                vm->frame_capacity = new_cap;
            }
            if ((int)argc!=fe->param_count) {
                vm_runtime_error(vm,LOC,"Function '%s' expects %d args, got %u",fe->name?fe->name:"?",fe->param_count,argc);
                for(uint32_t i=0;i<argc;i++) vm_pop_free(vm);
                vm_push(vm,value_null()); break;
            }
            CallFrame *frame=&vm->frames[vm->frame_top++];
            frame->return_ip=vm->pc+1; frame->stack_base=(uint32_t)vm->stack_top;
            int local_cap=fe->local_count>(int)argc?fe->local_count+8:(int)argc+8;
            frame->locals=ocl_malloc((size_t)local_cap*sizeof(Value));
            frame->local_count=(size_t)local_cap; frame->local_capacity=(size_t)local_cap;
            for(int i=0;i<local_cap;i++) frame->locals[i]=value_null();
            for(int i=(int)argc-1;i>=0;i--) {
                if(vm->stack_top>0) {
                    Value popped = vm_pop(vm);
                    if (popped.type == VALUE_STRING && !popped.owned)
                        frame->locals[i] = value_string_copy(popped.data.string_val);
                    else
                        frame->locals[i] = popped;
                }
            }
            vm->pc=fe->start_ip; continue;
        }

        case OP_RETURN: {
            Value ret_raw=vm_pop(vm);
            if (vm->frame_top==0) {
                if (ret_raw.type==VALUE_INT) vm->exit_code=(int)ret_raw.data.int_val;
                value_free(ret_raw); vm->halted=true; break;
            }
            Value ret_owned = (ret_raw.type == VALUE_STRING && !ret_raw.owned)
                              ? value_string_copy(ret_raw.data.string_val)
                              : ret_raw;
            CallFrame *frame=&vm->frames[--vm->frame_top];
            uint32_t ret_ip=frame->return_ip;
            for(size_t i=0;i<frame->local_count;i++) value_free(frame->locals[i]);
            ocl_free(frame->locals);
            while(vm->stack_top>frame->stack_base) vm_pop_free(vm);
            vm_push(vm,ret_owned); vm->pc=ret_ip; continue;
        }

        case OP_HALT:
            vm->halted=true;
            if (vm->stack_top>0) {
                Value top=vm_peek(vm,0);
                if (top.type==VALUE_INT) vm->exit_code=(int)top.data.int_val;
                else if (top.type==VALUE_BOOL) vm->exit_code=top.data.bool_val?1:0;
            }
            break;

        case OP_CALL_BUILTIN: {
            int bid=(int)ins.operand1, argc=(int)ins.operand2;
            switch(bid) {
                case BUILTIN_PRINT:  builtin_print(vm,argc);  break;
                case BUILTIN_PRINTF: builtin_printf(vm,argc); break;
                default:
                    if (!stdlib_dispatch(vm,bid,argc)) {
                        vm_runtime_error(vm,LOC,"Unknown built-in function id %d",bid);
                        for(int i=0;i<argc;i++) vm_pop_free(vm);
                        vm_push(vm,value_null());
                    }
                    break;
            }
            break;
        }

        case OP_TO_INT: {
            Value a=vm_pop(vm); int64_t r;
            switch(a.type) {
                case VALUE_INT:    r=a.data.int_val; break;
                case VALUE_FLOAT:  r=(int64_t)a.data.float_val; break;
                case VALUE_BOOL:   r=a.data.bool_val?1:0; break;
                case VALUE_CHAR:   r=(int64_t)(unsigned char)a.data.char_val; break;
                case VALUE_STRING: r=a.data.string_val?(int64_t)strtoll(a.data.string_val,NULL,10):0; break;
                default:           r=0; break;
            }
            value_free(a); vm_push(vm,value_int(r)); break;
        }
        case OP_TO_FLOAT: {
            Value a=vm_pop(vm); double r;
            switch(a.type) {
                case VALUE_FLOAT:  r=a.data.float_val; break;
                case VALUE_INT:    r=(double)a.data.int_val; break;
                case VALUE_BOOL:   r=a.data.bool_val?1.0:0.0; break;
                case VALUE_CHAR:   r=(double)(unsigned char)a.data.char_val; break;
                case VALUE_STRING: r=a.data.string_val?strtod(a.data.string_val,NULL):0.0; break;
                default:           r=0.0; break;
            }
            value_free(a); vm_push(vm,value_float(r)); break;
        }
        case OP_TO_STRING: {
            Value a=vm_pop(vm);
            char *owned = ocl_strdup(value_to_string(a));
            value_free(a);
            vm_push(vm,value_string(owned));
            break;
        }

        case OP_ARRAY_NEW: {
            uint32_t count = ins.operand1;
            /* ocl_array_new sets refcount=1 (creator reference).
             * value_array() calls ocl_array_retain → refcount=2.
             * We must release the creator reference after the push
             * so the array is solely owned by the stack value. */
            OclArray *arr = ocl_array_new(count > 0 ? (size_t)count : 8);
            if (count > 0) {
                Value *tmp = ocl_malloc((size_t)count * sizeof(Value));
                for (uint32_t i = count; i > 0; i--) tmp[i-1] = vm_pop(vm);
                for (uint32_t i = 0; i < count; i++) { ocl_array_push(arr, tmp[i]); value_free(tmp[i]); }
                ocl_free(tmp);
            }
            vm_push(vm, value_array(arr));
            ocl_array_release(arr); /* drop creator reference; stack value holds the only ref now */
            break;
        }

        case OP_ARRAY_GET: {
            Value idx_v = vm_pop(vm);
            Value arr_v = vm_pop(vm);

            if (arr_v.type == VALUE_STRING) {
                if (idx_v.type != VALUE_INT) {
                    vm_runtime_error(vm, LOC, "OP_ARRAY_GET: string index must be Int (got %s)", value_type_name(idx_v.type));
                    value_free(idx_v); value_free(arr_v); vm_push(vm, value_null()); break;
                }
                const char *s = arr_v.data.string_val ? arr_v.data.string_val : "";
                int64_t idx = idx_v.data.int_val;
                size_t slen = strlen(s);
                if (idx < 0 || (size_t)idx >= slen) {
                    vm_runtime_error(vm, LOC, "OP_ARRAY_GET: string index %lld out of bounds [0, %zu)", (long long)idx, slen);
                    value_free(idx_v); value_free(arr_v); vm_push(vm, value_null()); break;
                }
                char ch = s[idx];
                value_free(idx_v); value_free(arr_v);
                vm_push(vm, value_char(ch));
                break;
            }

            if (arr_v.type != VALUE_ARRAY || !arr_v.data.array_val) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_GET: value is not an Array or String (got %s)", value_type_name(arr_v.type));
                value_free(idx_v); value_free(arr_v); vm_push(vm, value_null()); break;
            }
            if (idx_v.type != VALUE_INT) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_GET: index must be Int (got %s)", value_type_name(idx_v.type));
                value_free(idx_v); value_free(arr_v); vm_push(vm, value_null()); break;
            }
            int64_t idx = idx_v.data.int_val;
            OclArray *arr = arr_v.data.array_val;
            if (idx < 0 || (size_t)idx >= arr->length) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_GET: index %lld out of bounds [0, %zu)", (long long)idx, arr->length);
                value_free(idx_v); value_free(arr_v); vm_push(vm, value_null()); break;
            }
            Value elem = ocl_array_get(arr, (size_t)idx);
            value_free(idx_v); value_free(arr_v);
            vm_push(vm, elem);
            break;
        }

        case OP_ARRAY_SET: {
            Value idx_v = vm_pop(vm);
            Value arr_v = vm_pop(vm);
            Value val   = vm_pop(vm);
            if (arr_v.type != VALUE_ARRAY || !arr_v.data.array_val) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_SET: not an Array (got %s)", value_type_name(arr_v.type));
                value_free(idx_v); value_free(arr_v); value_free(val); break;
            }
            if (idx_v.type != VALUE_INT) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_SET: index must be Int (got %s)", value_type_name(idx_v.type));
                value_free(idx_v); value_free(arr_v); value_free(val); break;
            }
            int64_t idx = idx_v.data.int_val;
            if (idx < 0) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_SET: negative index %lld", (long long)idx);
                value_free(idx_v); value_free(arr_v); value_free(val); break;
            }
            ocl_array_set(arr_v.data.array_val, (size_t)idx, val);
            value_free(idx_v); value_free(arr_v); value_free(val);
            break;
        }

        case OP_ARRAY_LEN: {
            Value arr_v = vm_pop(vm);
            if (arr_v.type == VALUE_STRING) {
                int64_t len = (int64_t)(arr_v.data.string_val ? strlen(arr_v.data.string_val) : 0);
                value_free(arr_v);
                vm_push(vm, value_int(len));
                break;
            }
            if (arr_v.type != VALUE_ARRAY || !arr_v.data.array_val) {
                vm_runtime_error(vm, LOC, "OP_ARRAY_LEN: not an Array (got %s)", value_type_name(arr_v.type));
                value_free(arr_v); vm_push(vm, value_int(0)); break;
            }
            int64_t len = (int64_t)arr_v.data.array_val->length;
            value_free(arr_v);
            vm_push(vm, value_int(len));
            break;
        }

        default:
            vm_runtime_error(vm, LOC, "Unknown opcode %d at ip=%u", ins.opcode, vm->pc);
            break;
        }

        vm->pc++;
    }

#undef LOC
#undef ARITH_OP
#undef CMP_OP

    return vm->exit_code;
}

Value vm_get_result(VM *vm) {
    if (vm->stack_top > 0) return vm->stack[vm->stack_top - 1];
    return value_null();
}