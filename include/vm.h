#ifndef OCL_VM_H
#define OCL_VM_H

#include "common.h"
#include "bytecode.h"

/* ── Limits ───────────────────────────────────────────────────────── */
#define VM_STACK_MAX        4096   /* maximum depth of the value stack  */
#define VM_FRAMES_INITIAL    256   /* initial call-frame capacity        */
#define VM_FRAMES_MAX       4096   /* hard cap: "call stack overflow"   */
#define VM_FRAMES_GROW          2  /* growth factor when frames[] is full */

/* ── Call frame ───────────────────────────────────────────────────── */
typedef struct {
    uint32_t return_ip;     /* instruction index to return to     */
    uint32_t stack_base;    /* vm->stack_top on entry (for unwind) */
    Value   *locals;        /* local variable slots               */
    size_t   local_count;   /* number of live slots               */
    size_t   local_capacity;
} CallFrame;

/* ── Virtual machine ──────────────────────────────────────────────── */
typedef struct VM {
    Bytecode  *bytecode;

    /* Value stack */
    Value  stack[VM_STACK_MAX];
    size_t stack_top;

    /* Call-frame stack (heap-allocated, grows dynamically) */
    CallFrame *frames;
    size_t     frame_top;
    size_t     frame_capacity;

    /* Global variable slots */
    Value  *globals;
    size_t  global_count;
    size_t  global_capacity;

    /* Execution state */
    uint32_t pc;
    bool     halted;
    int      exit_code;
} VM;

/* ── Public API ───────────────────────────────────────────────────── */
VM   *vm_create(Bytecode *bytecode);
void  vm_free(VM *vm);
int   vm_execute(VM *vm);

/* Stack operations — exposed for built-in functions */
void  vm_push(VM *vm, Value v);
Value vm_pop(VM *vm);
Value vm_peek(VM *vm, size_t depth);  /* depth 0 = top */

#endif /* OCL_VM_H */
