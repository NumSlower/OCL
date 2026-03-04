#ifndef OCL_CODEGEN_H
#define OCL_CODEGEN_H

#include "ast.h"
#include "bytecode.h"
#include "errors.h"

/*
 * VarSlot — a named variable mapped to a bytecode slot index.
 * Locals and globals are tracked separately in the CodeGenerator.
 */
typedef struct {
    char *name;       /* heap-owned */
    int   slot;       /* index into the frame's locals[] or globals[] */
    int   scope_level;
} VarSlot;

/* A built-in function name mapped to its dispatch ID. */
typedef struct {
    char *name; /* borrowed pointer — do not free */
    int   id;
} BuiltinDesc;

/* Maximum number of pending break/continue jumps per loop. */
#define CODEGEN_MAX_BREAKS 256

/*
 * PendingJump — one unresolved forward jump waiting to be backpatched.
 * `patch_idx` is the instruction index of the OP_JUMP whose operand1
 * will be filled in once the target address is known.
 */
typedef struct {
    uint32_t patch_idx;
} PendingJump;

/*
 * LoopContext — tracks break and continue jumps for one loop level.
 *
 * At the start of each loop, a LoopContext is pushed onto loop_stack[].
 * As break/continue statements are encountered, their OP_JUMP instructions
 * are recorded here.  When the loop body finishes, all pending jumps are
 * backpatched in one pass.
 */
typedef struct {
    uint32_t    continue_target; /* instruction index of the loop's continue point */
    bool        continue_known;  /* true once continue_target has been set         */

    PendingJump breaks[CODEGEN_MAX_BREAKS];
    int         break_count;

    PendingJump continues[CODEGEN_MAX_BREAKS];
    int         continue_count;
} LoopContext;

/* Maximum loop nesting depth. */
#define CODEGEN_MAX_LOOP_DEPTH 64

/*
 * CodeGenerator — state threaded through the entire code-generation pass.
 *
 * Variables:
 *   vars[]         — all locals visible at the current point (scope-tracked)
 *   globals[]      — all top-level variables
 *   local_stack[]  — per-function local-slot counter stack; top element is the
 *                    next free slot for the innermost function being emitted
 *   scope_level    — current lexical scope depth (0 = global)
 *   in_global_scope — true when emitting top-level (non-function) code
 */
typedef struct {
    Bytecode       *bytecode;
    ErrorCollector *errors;

    VarSlot *vars;
    size_t   var_count;
    size_t   var_cap;
    int      scope_level;

    /* local_stack: one entry per active function being emitted.
       Entry value = next available local slot index for that function. */
#define CODEGEN_LOCAL_STACK_MAX 256
    int local_stack[CODEGEN_LOCAL_STACK_MAX];
    int local_stack_top;
    bool in_global_scope;

    VarSlot *globals;
    size_t   global_count;
    size_t   global_cap;

    BuiltinDesc *builtins;
    size_t       builtin_count;

    LoopContext loop_stack[CODEGEN_MAX_LOOP_DEPTH];
    int         loop_depth;
} CodeGenerator;

/* ── Public API ───────────────────────────────────────────────────── */

CodeGenerator *codegen_create(ErrorCollector *errors);
void           codegen_free(CodeGenerator *gen);

/*
 * codegen_generate — walk `program` and emit bytecode into `output`.
 * Returns true on success; errors are reported via the ErrorCollector.
 */
bool           codegen_generate(CodeGenerator *gen, ProgramNode *program, Bytecode *output);

#endif /* OCL_CODEGEN_H */