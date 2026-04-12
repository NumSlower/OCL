#ifndef OCL_BYTECODE_H
#define OCL_BYTECODE_H

#include "common.h"

typedef enum {
    OP_PUSH_CONST,
    OP_DUP,
    OP_POP,
    OP_LOAD_VAR,
    OP_STORE_VAR,
    OP_LOAD_CAPTURE,
    OP_STORE_CAPTURE,
    OP_LOAD_GLOBAL,
    OP_STORE_GLOBAL,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_NOT,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_AND,
    OP_OR,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_JUMP_IF_NOT_NULL,
    OP_CALL,
    OP_CALL_VALUE,
    OP_RETURN,
    OP_HALT,
    OP_ARRAY_NEW,
    OP_ARRAY_GET,
    OP_ARRAY_SET,
    OP_ARRAY_LEN,
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_LSHIFT,
    OP_RSHIFT,
    OP_STRUCT_NEW,
    OP_STRUCT_GET,
    OP_STRUCT_SET,
    OP_MAKE_FUNCTION,
} Opcode;

typedef enum {
    FUNC_CAPTURE_LOCAL,
    FUNC_CAPTURE_CAPTURE
} FuncCaptureSource;

typedef struct {
    char             *name;
    FuncCaptureSource source;
    uint32_t          slot;
} FuncCapture;

typedef struct {
    Opcode         opcode;
    uint32_t       operand1;
    uint32_t       operand2;
    SourceLocation location;
} Instruction;

typedef struct {
    char    *name;
    uint32_t start_ip;
    int      param_count;
    int      local_count;
    char   **local_names;
    size_t   local_name_count;
    FuncCapture *captures;
    size_t       capture_count;
} FuncEntry;

#define OCL_FUNC_UNRESOLVED 0xFFFFFFFFu
#define OCL_FUNC_BUILTIN    0xFFFFFFFEu

typedef struct {
    Instruction *instructions;
    size_t       instruction_count;
    size_t       instruction_capacity;
    Value       *constants;
    size_t       constant_count;
    size_t       constant_capacity;
    FuncEntry   *functions;
    size_t       function_count;
    size_t       function_capacity;
} Bytecode;

Bytecode *bytecode_create(void);
void      bytecode_free(Bytecode *bc);
void      bytecode_emit(Bytecode *bc, Opcode op, uint32_t operand1, uint32_t operand2, SourceLocation loc);
void      bytecode_patch(Bytecode *bc, uint32_t index, uint32_t new_operand1);
uint32_t  bytecode_add_constant(Bytecode *bc, Value v);
uint32_t  bytecode_add_function(Bytecode *bc, const char *name, uint32_t start_ip, int param_count);
int       bytecode_find_function(Bytecode *bc, const char *name);
void      bytecode_dump(Bytecode *bc);
bool      bytecode_write_executable(Bytecode *bc, const char *path);
Bytecode *bytecode_read_executable(const char *path, bool *is_executable);

#endif
