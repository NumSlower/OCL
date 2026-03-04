#ifndef OCL_BYTECODE_H
#define OCL_BYTECODE_H

#include "common.h"

/*
 * Opcode set for the OCL stack-based VM.
 *
 * Each instruction is stored as an `Instruction` with two 32-bit operands
 * and a source location for error reporting.
 */
typedef enum {
    /* ── Stack manipulation ───────────────────────────────── */
    OP_PUSH_CONST,      /* operand1 = constant pool index              */
    OP_POP,             /* discard top of stack                        */

    /* ── Variable access ─────────────────────────────────── */
    OP_LOAD_VAR,        /* operand1 = local slot index                 */
    OP_STORE_VAR,       /* operand1 = local slot index                 */
    OP_LOAD_GLOBAL,     /* operand1 = global slot index                */
    OP_STORE_GLOBAL,    /* operand1 = global slot index                */

    /* ── Arithmetic ───────────────────────────────────────── */
    OP_ADD,             /* Int+Int, Float+Float, or String concat       */
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,          /* integer division truncates toward zero       */
    OP_MODULO,          /* Int only; runtime error on Float operands    */
    OP_NEGATE,          /* unary minus                                  */

    /* ── Logical ──────────────────────────────────────────── */
    OP_NOT,             /* logical NOT (works on any value via truthy)  */

    /* ── Comparison (all push Bool) ──────────────────────── */
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,

    /* ── Control flow ─────────────────────────────────────── */
    OP_JUMP,            /* operand1 = target instruction index          */
    OP_JUMP_IF_FALSE,   /* pop + jump if falsy;  used by if/while/for  */
    OP_JUMP_IF_TRUE,    /* pop + jump if truthy; used by short-circuit ||*/

    /* ── Calls and return ─────────────────────────────────── */
    OP_CALL,            /* operand1=func index, operand2=argc           */
    OP_RETURN,          /* pop return value, restore caller frame       */
    OP_HALT,            /* stop execution; top Int becomes exit code    */
    OP_CALL_BUILTIN,    /* operand1=builtin id, operand2=argc           */

    /* ── Array operations ─────────────────────────────────── */
    OP_ARRAY_NEW,       /* operand1=element count; pops elements        */
    OP_ARRAY_GET,       /* stack: [array/string, index] → value/char   */
    OP_ARRAY_SET,       /* stack: [value, array, index]; mutates array  */
    OP_ARRAY_LEN,       /* stack: [array/string] → Int                 */
} Opcode;

/* A single bytecode instruction. */
typedef struct {
    Opcode       opcode;
    uint32_t     operand1;
    uint32_t     operand2;
    SourceLocation location;  /* for runtime error messages */
} Instruction;

/* An entry in the function table. */
typedef struct {
    char    *name;          /* heap-owned, freed by bytecode_free */
    uint32_t start_ip;      /* index into instructions[] of first instruction */
    int      param_count;   /* number of parameters (= first N local slots)   */
    int      local_count;   /* total local slots used (set by codegen)         */
} FuncEntry;

/* The compiled bytecode chunk: instructions + constants + function table. */
typedef struct {
    Instruction *instructions;
    size_t       instruction_count;
    size_t       instruction_capacity;

    Value   *constants;
    size_t   constant_count;
    size_t   constant_capacity;

    FuncEntry *functions;
    size_t     function_count;
    size_t     function_capacity;
} Bytecode;

/* ── Public API ───────────────────────────────────────────────────── */

Bytecode *bytecode_create(void);
void      bytecode_free(Bytecode *bc);

/* Append one instruction and return its index. */
void      bytecode_emit(Bytecode *bc, Opcode op,
                         uint32_t operand1, uint32_t operand2,
                         SourceLocation loc);

/* Retroactively patch operand1 of instruction at `index` (used for backpatching jumps). */
void      bytecode_patch(Bytecode *bc, uint32_t index, uint32_t new_operand1);

/* Add a value to the constant pool and return its index. */
uint32_t  bytecode_add_constant(Bytecode *bc, Value v);

/* Register a function; returns its index. If the name already exists, updates it. */
uint32_t  bytecode_add_function(Bytecode *bc, const char *name,
                                 uint32_t start_ip, int param_count);

/* Look up a function by name; returns its index or -1 if not found. */
int       bytecode_find_function(Bytecode *bc, const char *name);

/* Print a human-readable disassembly to stdout (for --dump-bytecode). */
void      bytecode_dump(Bytecode *bc);

#endif /* OCL_BYTECODE_H */