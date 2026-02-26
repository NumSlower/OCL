/*
 * runtime.c — OCL Runtime Support
 *
 * Manages frame allocation, global variables, and runtime utilities
 * used by the VM outside of the main execution loop.
 */

#include "runtime.h"
#include "vm.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════
   Frame management
═══════════════════════════════════════════════════════════════════ */

/* Allocate a fresh call frame for a function with param_count parameters
   and total_locals local slots. Arguments must be loaded by the caller
   (the VM's OP_CALL handler) after this returns. */
CallFrame *runtime_frame_alloc(int total_locals) {
    int cap = total_locals > 0 ? total_locals + 8 : 8;
    CallFrame *f    = ocl_malloc(sizeof(CallFrame));
    f->locals       = ocl_malloc((size_t)cap * sizeof(Value));
    f->local_count  = (size_t)cap;
    f->local_capacity = (size_t)cap;
    f->return_ip    = 0;
    f->stack_base   = 0;
    for (int i = 0; i < cap; i++)
        f->locals[i] = value_null();
    return f;
}

/* Free a call frame and its local storage. */
void runtime_frame_free(CallFrame *f) {
    if (!f) return;
    ocl_free(f->locals);
    ocl_free(f);
}

/* Ensure a local slot exists in frame f, growing if needed. */
void runtime_ensure_local(CallFrame *f, uint32_t idx) {
    if (idx < (uint32_t)f->local_capacity) {
        if (idx >= (uint32_t)f->local_count) f->local_count = idx + 1;
        return;
    }
    uint32_t new_cap = idx + 16;
    f->locals = ocl_realloc(f->locals, new_cap * sizeof(Value));
    for (uint32_t i = (uint32_t)f->local_capacity; i < new_cap; i++)
        f->locals[i] = value_null();
    f->local_capacity = new_cap;
    f->local_count    = idx + 1;
}

/* ═══════════════════════════════════════════════════════════════════
   Global variable management
═══════════════════════════════════════════════════════════════════ */

/* Ensure global slot idx exists in the VM, growing if needed. */
void runtime_ensure_global(VM *vm, uint32_t idx) {
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

Value runtime_get_global(VM *vm, uint32_t idx) {
    runtime_ensure_global(vm, idx);
    return vm->globals[idx];
}

void runtime_set_global(VM *vm, uint32_t idx, Value v) {
    runtime_ensure_global(vm, idx);
    vm->globals[idx] = v;
}

/* ═══════════════════════════════════════════════════════════════════
   Runtime error reporting
═══════════════════════════════════════════════════════════════════ */

/* Print a formatted runtime error and mark the VM as halted. */
void runtime_error(VM *vm, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "RUNTIME ERROR: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    if (vm) {
        vm->halted    = true;
        vm->exit_code = 1;
    }
}

/* Print a stack trace from the current VM state. */
void runtime_stack_trace(VM *vm) {
    if (!vm) return;
    fprintf(stderr, "--- Stack trace (most recent call first) ---\n");
    for (int i = (int)vm->frame_top - 1; i >= 0; i--) {
        const char *name = "?";
        /* Try to find function name from bytecode function table */
        if (vm->bytecode) {
            for (size_t fi = 0; fi < vm->bytecode->function_count; fi++) {
                FuncEntry *fe = &vm->bytecode->functions[fi];
                /* The frame's return_ip - 1 should be inside the function */
                uint32_t ret = vm->frames[i].return_ip;
                if (ret > 0 && ret - 1 >= fe->start_ip) {
                    /* Find the function whose range contains this return address.
                       Use the last function whose start_ip <= ret - 1. */
                    name = fe->name;
                }
            }
        }
        fprintf(stderr, "  [%d] %s\n", i, name);
    }
    fprintf(stderr, "  [top] ip=%u\n", vm->pc);
}