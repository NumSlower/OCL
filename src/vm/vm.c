#include "vm.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>

VM *vm_create(Bytecode *bytecode) {
    VM *vm = ocl_malloc(sizeof(VM));
    vm->bytecode = bytecode;
    vm->stack = ocl_malloc(1024 * sizeof(Value));
    vm->stack_top = 0;
    vm->stack_capacity = 1024;
    vm->call_stack = ocl_malloc(256 * sizeof(CallFrame));
    vm->call_stack_top = 0;
    vm->call_stack_capacity = 256;
    vm->globals = NULL;
    vm->global_count = 0;
    vm->global_capacity = 0;
    vm->pc = 0;
    vm->halted = false;
    vm->exit_code = 0;
    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;
    
    ocl_free(vm->stack);
    for (size_t i = 0; i < vm->call_stack_top; i++) {
        ocl_free(vm->call_stack[i].locals);
    }
    ocl_free(vm->call_stack);
    for (size_t i = 0; i < vm->global_count; i++) {
        value_free(vm->globals[i]);
    }
    ocl_free(vm->globals);
    ocl_free(vm);
}

void vm_push(VM *vm, Value value) {
    if (!vm) return;
    
    if (vm->stack_top >= vm->stack_capacity) {
        vm->stack_capacity *= 2;
        vm->stack = ocl_realloc(vm->stack, vm->stack_capacity * sizeof(Value));
    }
    
    vm->stack[vm->stack_top++] = value;
}

Value vm_pop(VM *vm) {
    if (!vm || vm->stack_top == 0) {
        return value_null();
    }
    return vm->stack[--vm->stack_top];
}

Value vm_peek(VM *vm, size_t depth) {
    if (!vm || depth >= vm->stack_top) {
        return value_null();
    }
    return vm->stack[vm->stack_top - 1 - depth];
}

int vm_execute(VM *vm) {
    if (!vm || !vm->bytecode) return 1;
    
    while (!vm->halted && vm->pc < vm->bytecode->instruction_count) {
        Instruction *instr = &vm->bytecode->instructions[vm->pc];
        
        switch (instr->opcode) {
            /* Stack operations */
            case OP_PUSH_CONST: {
                if (instr->operand1 < vm->bytecode->constant_count) {
                    vm_push(vm, vm->bytecode->constants[instr->operand1]);
                }
                break;
            }
            case OP_POP: {
                vm_pop(vm);
                break;
            }
            
            /* Variables */
            case OP_LOAD_VAR: {
                if (vm->call_stack_top > 0) {
                    /* Load from current call frame */
                    CallFrame *frame = &vm->call_stack[vm->call_stack_top - 1];
                    if (instr->operand1 < frame->local_count) {
                        vm_push(vm, frame->locals[instr->operand1]);
                    } else {
                        vm_push(vm, value_null());
                    }
                } else {
                    /* Global scope - load from globals if supported */
                    if (instr->operand1 < vm->global_count) {
                        vm_push(vm, vm->globals[instr->operand1]);
                    } else {
                        vm_push(vm, value_null());
                    }
                }
                break;
            }
            case OP_STORE_VAR: {
                if (vm->stack_top > 0) {
                    Value val = vm_pop(vm);
                    if (vm->call_stack_top > 0) {
                        /* Store in current call frame */
                        CallFrame *frame = &vm->call_stack[vm->call_stack_top - 1];
                        if (instr->operand1 >= frame->local_capacity) {
                            frame->local_capacity = instr->operand1 + 10;
                            frame->locals = ocl_realloc(frame->locals, 
                                                       frame->local_capacity * sizeof(Value));
                        }
                        frame->locals[instr->operand1] = val;
                        if (instr->operand1 >= frame->local_count) {
                            frame->local_count = instr->operand1 + 1;
                        }
                    } else {
                        /* Store in globals */
                        if (instr->operand1 >= vm->global_capacity) {
                            vm->global_capacity = instr->operand1 + 10;
                            vm->globals = ocl_realloc(vm->globals, 
                                                     vm->global_capacity * sizeof(Value));
                        }
                        vm->globals[instr->operand1] = val;
                        if (instr->operand1 >= vm->global_count) {
                            vm->global_count = instr->operand1 + 1;
                        }
                    }
                }
                break;
            }
            
            /* Arithmetic */
            case OP_ADD: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        vm_push(vm, value_int(a.data.int_val + b.data.int_val));
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        vm_push(vm, value_float(af + bf));
                    }
                }
                break;
            }
            case OP_SUBTRACT: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        vm_push(vm, value_int(a.data.int_val - b.data.int_val));
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        vm_push(vm, value_float(af - bf));
                    }
                }
                break;
            }
            case OP_MULTIPLY: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        vm_push(vm, value_int(a.data.int_val * b.data.int_val));
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        vm_push(vm, value_float(af * bf));
                    }
                }
                break;
            }
            case OP_DIVIDE: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    if (b.type == VALUE_FLOAT ? b.data.float_val != 0 : b.data.int_val != 0) {
                        if (a.type == VALUE_INT && b.type == VALUE_INT) {
                            vm_push(vm, value_int(a.data.int_val / b.data.int_val));
                        } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                            double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                            double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                            vm_push(vm, value_float(af / bf));
                        }
                    }
                }
                break;
            }
            case OP_MODULO: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    if (a.type == VALUE_INT && b.type == VALUE_INT && b.data.int_val != 0) {
                        vm_push(vm, value_int(a.data.int_val % b.data.int_val));
                    }
                }
                break;
            }
            case OP_NEGATE: {
                if (vm->stack_top > 0) {
                    Value a = vm_pop(vm);
                    if (a.type == VALUE_INT) {
                        vm_push(vm, value_int(-a.data.int_val));
                    } else if (a.type == VALUE_FLOAT) {
                        vm_push(vm, value_float(-a.data.float_val));
                    }
                }
                break;
            }
            
            /* Comparison */
            case OP_EQUAL: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool result = false;
                    if (a.type == b.type) {
                        if (a.type == VALUE_INT) {
                            result = a.data.int_val == b.data.int_val;
                        } else if (a.type == VALUE_FLOAT) {
                            result = a.data.float_val == b.data.float_val;
                        } else if (a.type == VALUE_BOOL) {
                            result = a.data.bool_val == b.data.bool_val;
                        }
                    }
                    vm_push(vm, value_bool(result));
                }
                break;
            }
            case OP_NOT_EQUAL: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool result = true;
                    if (a.type == b.type) {
                        if (a.type == VALUE_INT) {
                            result = a.data.int_val != b.data.int_val;
                        } else if (a.type == VALUE_FLOAT) {
                            result = a.data.float_val != b.data.float_val;
                        } else if (a.type == VALUE_BOOL) {
                            result = a.data.bool_val != b.data.bool_val;
                        }
                    }
                    vm_push(vm, value_bool(result));
                }
                break;
            }
            case OP_LESS: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool result = false;
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        result = a.data.int_val < b.data.int_val;
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        result = af < bf;
                    }
                    vm_push(vm, value_bool(result));
                }
                break;
            }
            case OP_LESS_EQUAL: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool result = false;
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        result = a.data.int_val <= b.data.int_val;
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        result = af <= bf;
                    }
                    vm_push(vm, value_bool(result));
                }
                break;
            }
            case OP_GREATER: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool result = false;
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        result = a.data.int_val > b.data.int_val;
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        result = af > bf;
                    }
                    vm_push(vm, value_bool(result));
                }
                break;
            }
            case OP_GREATER_EQUAL: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool result = false;
                    if (a.type == VALUE_INT && b.type == VALUE_INT) {
                        result = a.data.int_val >= b.data.int_val;
                    } else if (a.type == VALUE_FLOAT || b.type == VALUE_FLOAT) {
                        double af = (a.type == VALUE_FLOAT) ? a.data.float_val : a.data.int_val;
                        double bf = (b.type == VALUE_FLOAT) ? b.data.float_val : b.data.int_val;
                        result = af >= bf;
                    }
                    vm_push(vm, value_bool(result));
                }
                break;
            }
            
            /* Logic */
            case OP_AND: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool ab = (a.type == VALUE_BOOL) ? a.data.bool_val : 
                              (a.type == VALUE_INT && a.data.int_val != 0);
                    bool bb = (b.type == VALUE_BOOL) ? b.data.bool_val : 
                              (b.type == VALUE_INT && b.data.int_val != 0);
                    vm_push(vm, value_bool(ab && bb));
                }
                break;
            }
            case OP_OR: {
                if (vm->stack_top >= 2) {
                    Value b = vm_pop(vm);
                    Value a = vm_pop(vm);
                    bool ab = (a.type == VALUE_BOOL) ? a.data.bool_val : 
                              (a.type == VALUE_INT && a.data.int_val != 0);
                    bool bb = (b.type == VALUE_BOOL) ? b.data.bool_val : 
                              (b.type == VALUE_INT && b.data.int_val != 0);
                    vm_push(vm, value_bool(ab || bb));
                }
                break;
            }
            case OP_NOT: {
                if (vm->stack_top > 0) {
                    Value a = vm_pop(vm);
                    bool ab = (a.type == VALUE_BOOL) ? a.data.bool_val : 
                              (a.type == VALUE_INT && a.data.int_val != 0);
                    vm_push(vm, value_bool(!ab));
                }
                break;
            }
            
            /* Control flow */
            case OP_JUMP: {
                vm->pc = instr->operand1;
                continue;
            }
            case OP_JUMP_IF_FALSE: {
                if (vm->stack_top > 0) {
                    Value cond = vm_pop(vm);
                    bool is_true = (cond.type == VALUE_BOOL) ? cond.data.bool_val : 
                                   (cond.type == VALUE_INT && cond.data.int_val != 0);
                    if (!is_true) {
                        vm->pc = instr->operand1;
                        continue;
                    }
                }
                break;
            }
            case OP_JUMP_IF_TRUE: {
                if (vm->stack_top > 0) {
                    Value cond = vm_pop(vm);
                    bool is_true = (cond.type == VALUE_BOOL) ? cond.data.bool_val : 
                                   (cond.type == VALUE_INT && cond.data.int_val != 0);
                    if (is_true) {
                        vm->pc = instr->operand1;
                        continue;
                    }
                }
                break;
            }
            
            /* Functions */
            case OP_RETURN: {
                vm->halted = true;
                if (vm->stack_top > 0) {
                    vm->exit_code = 0;  /* Get from stack if needed */
                    if (vm->stack[vm->stack_top - 1].type == VALUE_INT) {
                        vm->exit_code = (int)vm->stack[vm->stack_top - 1].data.int_val;
                    }
                }
                break;
            }
            case OP_CALL: {
                int builtin_id = instr->operand1;
                int arg_count = instr->operand2;
                
                if (builtin_id == 1) {  /* print() */
                    /* Pop arguments and print them */
                    if (arg_count > 0) {
                        Value arg = vm_pop(vm);
                        if (arg.type == VALUE_INT) {
                            printf("%ld", arg.data.int_val);
                        } else if (arg.type == VALUE_FLOAT) {
                            printf("%lf", arg.data.float_val);
                        } else if (arg.type == VALUE_STRING) {
                            printf("%s", arg.data.string_val);
                        } else if (arg.type == VALUE_BOOL) {
                            printf("%s", arg.data.bool_val ? "true" : "false");
                        } else if (arg.type == VALUE_CHAR) {
                            printf("%c", arg.data.char_val);
                        }
                    }
                    printf("\n");
                    vm_push(vm, value_null());  /* Return null */
                } else if (builtin_id == 2) {  /* printf() */
                    /* Collect all arguments first (they're in reverse on stack) */
                    Value *printf_args = ocl_malloc(arg_count * sizeof(Value));
                    for (int i = arg_count - 1; i >= 0; i--) {
                        if (vm->stack_top > 0) {
                            printf_args[i] = vm_pop(vm);
                        }
                    }
                    
                    if (arg_count > 0 && printf_args[0].type == VALUE_STRING) {
                        const char *fmt = printf_args[0].data.string_val;
                        int next_arg = 1;
                        
                        for (size_t i = 0; fmt[i]; i++) {
                            if (fmt[i] == '%' && fmt[i+1]) {
                                i++;
                                switch (fmt[i]) {
                                    case 'd': {  /* Integer */
                                        if (next_arg < arg_count && printf_args[next_arg].type == VALUE_INT) {
                                            printf("%ld", printf_args[next_arg].data.int_val);
                                        }
                                        next_arg++;
                                        break;
                                    }
                                    case 'f': {  /* Float */
                                        if (next_arg < arg_count && printf_args[next_arg].type == VALUE_FLOAT) {
                                            printf("%lf", printf_args[next_arg].data.float_val);
                                        }
                                        next_arg++;
                                        break;
                                    }
                                    case 's': {  /* String */
                                        if (next_arg < arg_count && printf_args[next_arg].type == VALUE_STRING) {
                                            printf("%s", printf_args[next_arg].data.string_val);
                                        }
                                        next_arg++;
                                        break;
                                    }
                                    case 'n': {  /* Newline */
                                        printf("\n");
                                        break;
                                    }
                                    case '%': {
                                        printf("%%");
                                        break;
                                    }
                                }
                            } else if (fmt[i] == '\\' && fmt[i+1] == 'n') {
                                printf("\n");
                                i++;
                            } else {
                                printf("%c", fmt[i]);
                            }
                        }
                    }
                    ocl_free(printf_args);
                    vm_push(vm, value_null());  /* Return null */
                } else {
                    /* User-defined function - create call frame */
                    if (vm->call_stack_top >= vm->call_stack_capacity) {
                        vm->call_stack_capacity *= 2;
                        vm->call_stack = ocl_realloc(vm->call_stack, 
                                                    vm->call_stack_capacity * sizeof(CallFrame));
                    }
                    
                    CallFrame *new_frame = &vm->call_stack[vm->call_stack_top++];
                    new_frame->return_address = vm->pc + 1;
                    new_frame->locals = ocl_malloc(64 * sizeof(Value));
                    new_frame->local_count = arg_count;
                    new_frame->local_capacity = 64;
                    
                    /* Pop arguments in reverse order */
                    for (int i = arg_count - 1; i >= 0; i--) {
                        if (vm->stack_top > 0) {
                            new_frame->locals[i] = vm_pop(vm);
                        }
                    }
                    
                    /* TODO: Jump to function bytecode */
                    vm->call_stack_top--;
                    ocl_free(new_frame->locals);
                }
                break;
            }
            case OP_HALT: {
                vm->halted = true;
                break;
            }
            
            default:
                fprintf(stderr, "ERROR: Unknown opcode %d at instruction %u\n", 
                        instr->opcode, vm->pc);
                return 1;
        }
        
        vm->pc++;
    }
    
    return vm->exit_code;
}

Value vm_get_result(VM *vm) {
    if (!vm || vm->stack_top == 0) {
        return value_null();
    }
    return vm_peek(vm, 0);
}
