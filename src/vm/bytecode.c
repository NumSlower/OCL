#include "bytecode.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>

Bytecode *bytecode_create(void) {
    Bytecode *bytecode = ocl_malloc(sizeof(Bytecode));
    bytecode->instructions = NULL;
    bytecode->instruction_count = 0;
    bytecode->capacity = 0;
    bytecode->constants = NULL;
    bytecode->constant_count = 0;
    bytecode->constant_capacity = 0;
    return bytecode;
}

void bytecode_free(Bytecode *bytecode) {
    if (!bytecode) return;
    
    ocl_free(bytecode->instructions);
    for (size_t i = 0; i < bytecode->constant_count; i++) {
        value_free(bytecode->constants[i]);
    }
    ocl_free(bytecode->constants);
    ocl_free(bytecode);
}

void bytecode_emit(Bytecode *bytecode, Opcode op, uint32_t operand1, uint32_t operand2, SourceLocation loc) {
    if (!bytecode) return;
    
    if (bytecode->instruction_count >= bytecode->capacity) {
        bytecode->capacity = (bytecode->capacity == 0) ? 256 : bytecode->capacity * 2;
        bytecode->instructions = ocl_realloc(bytecode->instructions, 
                                             bytecode->capacity * sizeof(Instruction));
    }
    
    Instruction *instr = &bytecode->instructions[bytecode->instruction_count];
    instr->opcode = op;
    instr->operand1 = operand1;
    instr->operand2 = operand2;
    instr->location = loc;
    
    bytecode->instruction_count++;
}

uint32_t bytecode_add_constant(Bytecode *bytecode, Value value) {
    if (!bytecode) return 0;
    
    if (bytecode->constant_count >= bytecode->constant_capacity) {
        bytecode->constant_capacity = (bytecode->constant_capacity == 0) ? 64 : bytecode->constant_capacity * 2;
        bytecode->constants = ocl_realloc(bytecode->constants, 
                                          bytecode->constant_capacity * sizeof(Value));
    }
    
    bytecode->constants[bytecode->constant_count] = value;
    return bytecode->constant_count++;
}

void bytecode_dump(Bytecode *bytecode) {
    if (!bytecode) return;
    
    printf("=== Bytecode Disassembly ===\n");
    printf("Instructions: %zu\n", bytecode->instruction_count);
    printf("Constants: %zu\n\n", bytecode->constant_count);
    
    const char *opcode_names[] = {
        "PUSH_CONST", "POP", "LOAD_VAR", "STORE_VAR", "LOAD_GLOBAL", 
        "STORE_GLOBAL", "ADD", "SUBTRACT", "MULTIPLY", "DIVIDE", "MODULO",
        "NEGATE", "NOT", "EQUAL", "NOT_EQUAL", "LESS", "LESS_EQUAL",
        "GREATER", "GREATER_EQUAL", "AND", "OR", "JUMP", "JUMP_IF_FALSE",
        "JUMP_IF_TRUE", "CALL", "RETURN", "HALT", "PRINT", "PRINTF",
        "TO_INT", "TO_FLOAT", "TO_STRING", "ARRAY_NEW", "ARRAY_GET",
        "ARRAY_SET", "ARRAY_LEN"
    };
    
    for (size_t i = 0; i < bytecode->instruction_count; i++) {
        Instruction *instr = &bytecode->instructions[i];
        int opcode_index = (int)instr->opcode;
        const char *opcode_name = (opcode_index >= 0 && opcode_index < 35) ? 
                                  opcode_names[opcode_index] : "UNKNOWN";
        
        printf("[%04zu] %-16s  (%u, %u)  [%d:%d]\n", 
               i, opcode_name, instr->operand1, instr->operand2,
               instr->location.line, instr->location.column);
    }
}
