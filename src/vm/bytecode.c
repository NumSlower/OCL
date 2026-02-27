#include "bytecode.h"
#include "common.h"
#include <stdio.h>
#include <string.h>

Bytecode *bytecode_create(void) {
    Bytecode *bc = ocl_malloc(sizeof(Bytecode));
    bc->instructions = NULL; bc->instruction_count = 0; bc->instruction_capacity = 0;
    bc->constants = NULL; bc->constant_count = 0; bc->constant_capacity = 0;
    bc->functions = NULL; bc->function_count = 0; bc->function_capacity = 0;
    return bc;
}

void bytecode_free(Bytecode *bc) {
    if (!bc) return;
    ocl_free(bc->instructions);
    for (size_t i = 0; i < bc->constant_count; i++) value_free(bc->constants[i]);
    ocl_free(bc->constants);
    for (size_t i = 0; i < bc->function_count; i++) ocl_free(bc->functions[i].name);
    ocl_free(bc->functions);
    ocl_free(bc);
}

void bytecode_emit(Bytecode *bc, Opcode op, uint32_t op1, uint32_t op2, SourceLocation loc) {
    if (!bc) return;
    if (bc->instruction_count >= bc->instruction_capacity) {
        bc->instruction_capacity = bc->instruction_capacity ? bc->instruction_capacity * 2 : 256;
        bc->instructions = ocl_realloc(bc->instructions, bc->instruction_capacity * sizeof(Instruction));
    }
    Instruction *ins = &bc->instructions[bc->instruction_count++];
    ins->opcode = op; ins->operand1 = op1; ins->operand2 = op2; ins->location = loc;
}

void bytecode_patch(Bytecode *bc, uint32_t index, uint32_t new_operand1) {
    if (!bc || index >= (uint32_t)bc->instruction_count) return;
    bc->instructions[index].operand1 = new_operand1;
}

uint32_t bytecode_add_constant(Bytecode *bc, Value v) {
    if (!bc) return 0;
    if (bc->constant_count >= bc->constant_capacity) {
        bc->constant_capacity = bc->constant_capacity ? bc->constant_capacity * 2 : 64;
        bc->constants = ocl_realloc(bc->constants, bc->constant_capacity * sizeof(Value));
    }
    if (v.type == VALUE_STRING && v.data.string_val)
        v.data.string_val = ocl_strdup(v.data.string_val);
    bc->constants[bc->constant_count] = v;
    return (uint32_t)bc->constant_count++;
}

uint32_t bytecode_add_function(Bytecode *bc, const char *name, uint32_t start_ip, int param_count) {
    if (!bc) return 0;
    for (size_t i = 0; i < bc->function_count; i++) {
        if (!strcmp(bc->functions[i].name, name)) {
            if (start_ip != 0xFFFFFFFF) bc->functions[i].start_ip = start_ip;
            bc->functions[i].param_count = param_count;
            return (uint32_t)i;
        }
    }
    if (bc->function_count >= bc->function_capacity) {
        bc->function_capacity = bc->function_capacity ? bc->function_capacity * 2 : 16;
        bc->functions = ocl_realloc(bc->functions, bc->function_capacity * sizeof(FuncEntry));
    }
    FuncEntry *fe = &bc->functions[bc->function_count];
    fe->name = ocl_strdup(name); fe->start_ip = start_ip; fe->param_count = param_count; fe->local_count = 0;
    return (uint32_t)bc->function_count++;
}

int bytecode_find_function(Bytecode *bc, const char *name) {
    if (!bc) return -1;
    for (size_t i = 0; i < bc->function_count; i++)
        if (!strcmp(bc->functions[i].name, name)) return (int)i;
    return -1;
}

void bytecode_dump(Bytecode *bc) {
    if (!bc) return;
    printf("=== Bytecode Disassembly ===\n");
    printf("Instructions: %zu\nConstants: %zu\nFunctions: %zu\n\n",
           bc->instruction_count, bc->constant_count, bc->function_count);
}
