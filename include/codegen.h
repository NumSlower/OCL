#ifndef OCL_CODEGEN_H
#define OCL_CODEGEN_H
#include "ast.h"
#include "bytecode.h"
#include "errors.h"
typedef struct { char *name; int slot; int scope_level; } VarSlot;
typedef struct { char *name; int id; } BuiltinDesc;
#define CODEGEN_MAX_FRAMES 256
#define CODEGEN_MAX_BREAKS 256
typedef struct {
    uint32_t  patch_idx;
} PendingJump;
typedef struct {
    uint32_t    continue_target;
    bool        continue_known;
    PendingJump breaks[CODEGEN_MAX_BREAKS];
    int         break_count;
    PendingJump continues[CODEGEN_MAX_BREAKS];
    int         continue_count;
} LoopContext;
#define CODEGEN_MAX_LOOP_DEPTH 64
typedef struct {
    Bytecode *bytecode; ErrorCollector *errors;
    VarSlot *vars; size_t var_count; size_t var_cap; int scope_level;
    int local_stack[CODEGEN_MAX_FRAMES]; int local_stack_top; bool in_global_scope;
    VarSlot *globals; size_t global_count; size_t global_cap;
    BuiltinDesc *builtins; size_t builtin_count;
    LoopContext loop_stack[CODEGEN_MAX_LOOP_DEPTH];
    int         loop_depth;
} CodeGenerator;
CodeGenerator *codegen_create(ErrorCollector *errors);
void           codegen_free(CodeGenerator *gen);
bool           codegen_generate(CodeGenerator *gen, ProgramNode *program, Bytecode *output);
#endif