#include "runtime.h"
#include "vm.h"
#include "common.h"
#include <stdio.h>
#include <string.h>

CallFrame *runtime_frame_alloc(int total_locals) {
    int cap = total_locals > 0 ? total_locals + 8 : 8;
    CallFrame *f = ocl_malloc(sizeof(CallFrame));
    f->locals = ocl_malloc((size_t)cap * sizeof(Value));
    f->local_count = (size_t)cap; f->local_capacity = (size_t)cap;
    f->return_ip = 0; f->stack_base = 0;
    for (int i = 0; i < cap; i++) f->locals[i] = value_null();
    return f;
}

void runtime_frame_free(CallFrame *f) {
    if (!f) return; ocl_free(f->locals); ocl_free(f);
}

void runtime_ensure_local(CallFrame *f, uint32_t idx) {
    if (idx < (uint32_t)f->local_capacity) {
        if (idx >= (uint32_t)f->local_count) f->local_count = idx + 1; return;
    }
    uint32_t new_cap = idx + 16;
    f->locals = ocl_realloc(f->locals, new_cap * sizeof(Value));
    for (uint32_t i = (uint32_t)f->local_capacity; i < new_cap; i++) f->locals[i] = value_null();
    f->local_capacity = new_cap; f->local_count = idx + 1;
}

void runtime_ensure_global(VM *vm, uint32_t idx) {
    if (idx >= vm->global_capacity) {
        uint32_t new_cap = idx + 16;
        vm->globals = ocl_realloc(vm->globals, new_cap * sizeof(Value));
        for (uint32_t i = (uint32_t)vm->global_capacity; i < new_cap; i++) vm->globals[i] = value_null();
        vm->global_capacity = new_cap;
    }
    if (idx >= (uint32_t)vm->global_count) vm->global_count = idx + 1;
}

Value runtime_get_global(VM *vm, uint32_t idx) { runtime_ensure_global(vm, idx); return vm->globals[idx]; }
void  runtime_set_global(VM *vm, uint32_t idx, Value v) { runtime_ensure_global(vm, idx); vm->globals[idx] = v; }

void runtime_error(VM *vm, const char *fmt, ...) {
    va_list ap; fprintf(stderr, "RUNTIME ERROR: ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fprintf(stderr, "\n");
    if (vm) { vm->halted = true; vm->exit_code = 1; }
}

void runtime_stack_trace(VM *vm) {
    if (!vm) return;
    fprintf(stderr, "--- Stack trace ---\n");
    fprintf(stderr, "  [top] ip=%u\n", vm->pc);
}
