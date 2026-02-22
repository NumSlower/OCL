#ifndef OCL_BYTECODE_H
#define OCL_BYTECODE_H

#include "common.h"
#include <stdint.h>

/* Bytecode instruction opcodes */
typedef enum {
    /* Stack operations */
    OP_PUSH_CONST,      /* Push constant onto stack */
    OP_POP,             /* Pop top of stack */
    
    /* Variables */
    OP_LOAD_VAR,        /* Load variable onto stack */
    OP_STORE_VAR,       /* Store top of stack into variable */
    OP_LOAD_GLOBAL,     /* Load global variable */
    OP_STORE_GLOBAL,    /* Store into global variable */
    
    /* Arithmetic and logic */
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_NOT,
    
    /* Comparison */
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    
    /* Logical */
    OP_AND,
    OP_OR,
    
    /* Control flow */
    OP_JUMP,            /* Unconditional jump */
    OP_JUMP_IF_FALSE,   /* Jump if top of stack is false */
    OP_JUMP_IF_TRUE,    /* Jump if top of stack is true */
    
    /* Functions */
    OP_CALL,            /* Call function by index */
    OP_RETURN,          /* Return from function */
    OP_HALT,            /* Stop execution */
    
    /* Built-in functions */
    OP_PRINT,           /* Print top of stack */
    OP_PRINTF,          /* Formatted print */
    
    /* Type conversions */
    OP_TO_INT,
    OP_TO_FLOAT,
    OP_TO_STRING,
    
    /* Array operations */
    OP_ARRAY_NEW,       /* Create new array */
    OP_ARRAY_GET,       /* Get element from array */
    OP_ARRAY_SET,       /* Set element in array */
    OP_ARRAY_LEN,       /* Get array length */
} Opcode;

/* Bytecode instruction */
typedef struct {
    Opcode opcode;
    uint32_t operand1;  /* Generic operand 1 (variable index, constant index, jump target, etc.) */
    uint32_t operand2;  /* Generic operand 2 (for operations with 2 operands) */
    SourceLocation location;  /* Source location for error reporting */
} Instruction;

/* Bytecode chunk (sequence of instructions) */
typedef struct {
    Instruction *instructions;
    size_t instruction_count;
    size_t capacity;
    Value *constants;
    size_t constant_count;
    size_t constant_capacity;
} Bytecode;

/* Bytecode functions */
Bytecode *bytecode_create(void);
void bytecode_free(Bytecode *bytecode);
void bytecode_emit(Bytecode *bytecode, Opcode op, uint32_t operand1, uint32_t operand2, SourceLocation loc);
uint32_t bytecode_add_constant(Bytecode *bytecode, Value value);
void bytecode_dump(Bytecode *bytecode);  /* For debugging */

#endif /* OCL_BYTECODE_H */
