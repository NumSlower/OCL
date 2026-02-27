#ifndef OCL_RUNTIME_H
#define OCL_RUNTIME_H
#include "common.h"
#include "vm.h"
#include <stdarg.h>
CallFrame *runtime_frame_alloc(int total_locals);
void       runtime_frame_free(CallFrame *f);
void       runtime_ensure_local(CallFrame *f, uint32_t idx);
void  runtime_ensure_global(VM *vm, uint32_t idx);
Value runtime_get_global(VM *vm, uint32_t idx);
void  runtime_set_global(VM *vm, uint32_t idx, Value v);
void runtime_error(VM *vm, const char *fmt, ...);
void runtime_stack_trace(VM *vm);
#endif
