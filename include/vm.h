#ifndef OCL_VM_H
#define OCL_VM_H

#include "bytecode.h"

/* Call frame for function calls */
typedef struct {
    uint32_t return_address;
    Value *locals;
    size_t local_count;
    size_t local_capacity;
} CallFrame;

/* Virtual machine */
typedef struct {
    Bytecode *bytecode;
    Value *stack;
    size_t stack_top;
    size_t stack_capacity;
    
    CallFrame *call_stack;
    size_t call_stack_top;
    size_t call_stack_capacity;
    
    Value *globals;
    size_t global_count;
    size_t global_capacity;
    
    uint32_t pc;  /* Program counter */
    bool halted;
    int exit_code;
} VM;

/* VM functions */
VM *vm_create(Bytecode *bytecode);
void vm_free(VM *vm);
int vm_execute(VM *vm);
Value vm_get_result(VM *vm);

/* Stack operations */
void vm_push(VM *vm, Value value);
Value vm_pop(VM *vm);
Value vm_peek(VM *vm, size_t depth);

#endif /* OCL_VM_H */
