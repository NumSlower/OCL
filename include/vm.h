#ifndef OCL_VM_H
#define OCL_VM_H
#include "common.h"
#include "bytecode.h"
#define VM_STACK_MAX  1024
#define VM_FRAMES_MAX 256
typedef struct {
    uint32_t return_ip; uint32_t stack_base;
    Value *locals; size_t local_count; size_t local_capacity;
} CallFrame;
typedef struct VM {
    Bytecode  *bytecode;
    Value      stack[VM_STACK_MAX]; size_t stack_top;
    CallFrame  frames[VM_FRAMES_MAX]; size_t frame_top;
    Value     *globals; size_t global_count; size_t global_capacity;
    uint32_t   pc; bool halted; int exit_code;
} VM;
VM    *vm_create(Bytecode *bytecode);
void   vm_free(VM *vm);
int    vm_execute(VM *vm);
Value  vm_get_result(VM *vm);
void   vm_push(VM *vm, Value v);
Value  vm_pop(VM *vm);
Value  vm_peek(VM *vm, size_t depth);
#endif
