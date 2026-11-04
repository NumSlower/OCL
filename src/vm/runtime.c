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
    if (!f) return;
    ocl_free(f->locals);
    ocl_free(f);
}

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
    f->local_count = idx + 1;
}

void runtime_ensure_global(VM *vm, uint32_t idx) {
    if (idx >= vm->global_capacity) {
        uint32_t new_cap = idx + 16;
        vm->globals = ocl_realloc(vm->globals, new_cap * sizeof(Value));
        for (uint32_t i = (uint32_t)vm->global_capacity; i < new_cap; i++)
            vm->globals[i] = value_null();
        vm->global_capacity = new_cap;
    }
    if (idx >= (uint32_t)vm->global_count) vm->global_count = idx + 1;
}

Value runtime_get_global(VM *vm, uint32_t idx) {
    runtime_ensure_global(vm, idx);
    return vm->globals[idx];
}

void runtime_set_global(VM *vm, uint32_t idx, Value v) {
    runtime_ensure_global(vm, idx);
    vm->globals[idx] = v;
}

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

/*
 * runtime_stack_trace — print the full call-frame stack with function names,
 * return addresses, local variable counts, and source locations.
 *
 * Frame 0 is the innermost (most-recently-called) frame; the last frame
 * printed is the outermost caller.  The current pc is shown for the live
 * frame since it hasn't stored a return_ip yet.
 */
void runtime_stack_trace(VM *vm) {
    if (!vm) return;

    fprintf(stderr, "--- Stack trace (innermost first) ---\n");

    if (vm->frame_top == 0) {
        fprintf(stderr, "  (no active call frames — executing at top level, ip=%u)\n",
                vm->pc);
        return;
    }

    for (size_t fi = vm->frame_top; fi-- > 0; ) {
        CallFrame *f = &vm->frames[fi];

        /* Try to resolve the function name by matching return_ip against the
           function table.  For the innermost frame we use vm->pc instead. */
        uint32_t    frame_ip  = (fi == vm->frame_top - 1) ? vm->pc : f->return_ip - 1;
        const char *func_name = NULL;

        if (vm->bytecode) {
            for (size_t fn = 0; fn < vm->bytecode->function_count; fn++) {
                FuncEntry *fe = &vm->bytecode->functions[fn];
                /* A frame belongs to function fn if its ip is within the
                   range [fe->start_ip, next_function_start_ip).  We use a
                   simple heuristic: find the function whose start_ip is
                   largest but still <= frame_ip. */
                if (fe->start_ip <= frame_ip) {
                    if (!func_name) {
                        func_name = fe->name;
                    } else {
                        /* Keep the function with the higher start_ip. */
                        for (size_t fn2 = 0; fn2 < vm->bytecode->function_count; fn2++) {
                            if (!strcmp(vm->bytecode->functions[fn2].name,
                                        func_name ? func_name : "")) {
                                if (fe->start_ip >
                                    vm->bytecode->functions[fn2].start_ip)
                                    func_name = fe->name;
                                break;
                            }
                        }
                    }
                }
            }
        }

        /* Source location from the current instruction (best effort). */
        SourceLocation loc = {0, 0, NULL};
        if (vm->bytecode &&
            frame_ip < (uint32_t)vm->bytecode->instruction_count)
            loc = vm->bytecode->instructions[frame_ip].location;

        fprintf(stderr, "  #%-3zu  %-24s  ip=%-6u  locals=%-3zu  return_ip=%-6u",
                vm->frame_top - 1 - fi,
                func_name ? func_name : "<unknown>",
                frame_ip,
                f->local_count,
                f->return_ip);

        if (loc.filename && loc.line > 0)
            fprintf(stderr, "  [%s:%d:%d]", loc.filename, loc.line, loc.column);

        fputc('\n', stderr);
    }

    fprintf(stderr, "-------------------------------------\n");
}
