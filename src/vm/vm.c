#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "common.h"
#include "errors.h"
#include "ocl_stdlib.h"
#include "runtime.h"

static void vm_clear_program_args(VM *vm);

static const char *vm_error_label(ErrorKind kind) {
    switch (kind) {
        case ERRK_LOGIC:     return "LOGIC ERROR";
        case ERRK_MEMORY:    return "MEMORY LEAK";
        case ERRK_TYPE:      return "TYPE ERROR";
        case ERRK_SYNTAX:    return "SYNTAX ERROR";
        case ERRK_OPERATION: return "OPERATION ERROR";
        default:             return "ERROR";
    }
}

bool ocl_vm_accepts_argc(int param_count, uint32_t argc) {
    if (OCL_ARGS_VARIADIC(param_count))
        return (int)argc >= OCL_ARGS_MIN(param_count);
    return param_count == (int)argc;
}

CallFrame *ocl_vm_current_frame(VM *vm) {
    if (!vm || vm->frame_top == 0) return NULL;
    return &vm->frames[vm->frame_top - 1];
}

void ocl_vm_ensure_local(CallFrame *frame, uint32_t idx) {
    if (!frame) return;

    if (idx < (uint32_t)frame->local_capacity) {
        if (idx >= (uint32_t)frame->local_count)
            frame->local_count = (size_t)idx + 1;
        return;
    }

    uint32_t new_cap = idx + 16;
    frame->locals = ocl_realloc(frame->locals, new_cap * sizeof(OclCell *));
    for (uint32_t i = (uint32_t)frame->local_capacity; i < new_cap; i++)
        frame->locals[i] = ocl_cell_new(value_null());
    frame->local_capacity = new_cap;
    frame->local_count = (size_t)idx + 1;
}

void ocl_vm_ensure_global(VM *vm, uint32_t idx) {
    if (!vm) return;

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
    vm->global_count = (size_t)idx + 1;
}

void ocl_vm_release_frame(CallFrame *frame) {
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

void ocl_vm_pop_free(VM *vm) {
    value_free(vm_pop(vm));
}

void ocl_vm_store_result(VM *vm, Value value) {
    if (!vm) return;

    if (vm->has_result)
        value_free(vm->result);
    if (value.type == VALUE_STRING)
        vm->result = value_string_copy(value.data.string_val);
    else
        vm->result = value_own_copy(value);
    vm->has_result = true;
}

void ocl_vm_discard_args_push_null(VM *vm, int n) {
    if (!vm) return;

    for (int i = 0; i < n; i++)
        ocl_vm_pop_free(vm);
    vm_push(vm, value_null());
}

void ocl_vm_report_error(VM *vm, ErrorKind kind, SourceLocation loc, const char *message) {
    const char *text = message ? message : "unknown runtime error";

    if (vm && vm->errors) {
        error_add(vm->errors, kind, ERROR_RUNTIME, loc, "%s", text);
    } else {
        const char *label = vm_error_label(kind);
        if (loc.filename && loc.line > 0)
            fprintf(stderr, "%s [%s:%d:%d]: %s\n",
                    label, loc.filename, loc.line, loc.column, text);
        else
            fprintf(stderr, "%s: %s\n", label, text);
    }

    if (vm) {
        vm->halted = true;
        vm->exit_code = 1;
        runtime_stack_trace(vm);
    }
}

VM *vm_create(Bytecode *bytecode, ErrorCollector *errors) {
    if (!bytecode) {
        fprintf(stderr, "ocl: vm_create: NULL bytecode\n");
        return NULL;
    }

    VM *vm = ocl_malloc(sizeof(VM));
    *vm = (VM){0};

    vm->bytecode = bytecode;
    vm->errors = errors;
    vm->frame_capacity = VM_FRAMES_INITIAL;
    vm->frames = ocl_malloc(vm->frame_capacity * sizeof(CallFrame));
    vm->result = value_null();
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

    for (size_t i = 0; i < vm->frame_top; i++)
        ocl_vm_release_frame(&vm->frames[i]);
    ocl_free(vm->frames);

    for (size_t i = 0; i < vm->global_count; i++)
        value_free(vm->globals[i]);
    ocl_free(vm->globals);

    value_free(vm->result);
    vm_clear_program_args(vm);
    ocl_free(vm);
}

void vm_push(VM *vm, Value v) {
    if (!vm) {
        value_free(v);
        return;
    }

    if (vm->stack_top >= VM_STACK_MAX) {
        char message[128];
        snprintf(message, sizeof(message),
                 "value stack overflow (depth=%llu, max=%d)",
                 (unsigned long long)vm->stack_top, VM_STACK_MAX);
        ocl_vm_report_error(vm, ERRK_OPERATION, LOC_NONE, message);
        value_free(v);
        return;
    }

    vm->stack[vm->stack_top++] = v;
}

Value vm_pop(VM *vm) {
    if (!vm || vm->stack_top == 0) {
        if (vm) {
            char message[96];
            snprintf(message, sizeof(message),
                     "value stack underflow at ip=%u", vm->pc);
            ocl_vm_report_error(vm, ERRK_LOGIC, LOC_NONE, message);
        }
        return value_null();
    }

    return vm->stack[--vm->stack_top];
}

Value vm_peek(VM *vm, size_t depth) {
    if (!vm || depth >= vm->stack_top)
        return value_null();
    return vm->stack[vm->stack_top - 1 - depth];
}
