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

void bytecode_patch(Bytecode *bc, uint32_t index, uint32_t new_op1) {
    if (!bc || index >= (uint32_t)bc->instruction_count) return;
    bc->instructions[index].operand1 = new_op1;
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

/* ── Full bytecode disassembler ───────────────────────────────────── */

static const char *opcode_name(Opcode op) {
    switch (op) {
        case OP_PUSH_CONST:      return "PUSH_CONST";
        case OP_POP:             return "POP";
        case OP_LOAD_VAR:        return "LOAD_VAR";
        case OP_STORE_VAR:       return "STORE_VAR";
        case OP_LOAD_GLOBAL:     return "LOAD_GLOBAL";
        case OP_STORE_GLOBAL:    return "STORE_GLOBAL";
        case OP_ADD:             return "ADD";
        case OP_SUBTRACT:        return "SUBTRACT";
        case OP_MULTIPLY:        return "MULTIPLY";
        case OP_DIVIDE:          return "DIVIDE";
        case OP_MODULO:          return "MODULO";
        case OP_NEGATE:          return "NEGATE";
        case OP_NOT:             return "NOT";
        case OP_EQUAL:           return "EQUAL";
        case OP_NOT_EQUAL:       return "NOT_EQUAL";
        case OP_LESS:            return "LESS";
        case OP_LESS_EQUAL:      return "LESS_EQUAL";
        case OP_GREATER:         return "GREATER";
        case OP_GREATER_EQUAL:   return "GREATER_EQUAL";
        case OP_AND:             return "AND";
        case OP_OR:              return "OR";
        case OP_JUMP:            return "JUMP";
        case OP_JUMP_IF_FALSE:   return "JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE:    return "JUMP_IF_TRUE";
        case OP_CALL:            return "CALL";
        case OP_RETURN:          return "RETURN";
        case OP_HALT:            return "HALT";
        case OP_CALL_BUILTIN:    return "CALL_BUILTIN";
        case OP_TO_INT:          return "TO_INT";
        case OP_TO_FLOAT:        return "TO_FLOAT";
        case OP_TO_STRING:       return "TO_STRING";
        case OP_ARRAY_NEW:       return "ARRAY_NEW";
        case OP_ARRAY_GET:       return "ARRAY_GET";
        case OP_ARRAY_SET:       return "ARRAY_SET";
        case OP_ARRAY_LEN:       return "ARRAY_LEN";
        default:                 return "UNKNOWN";
    }
}

void bytecode_dump(Bytecode *bc) {
    if (!bc) return;

    printf("=== Bytecode Disassembly ===\n");
    printf("Instructions: %zu  Constants: %zu  Functions: %zu\n\n",
           bc->instruction_count, bc->constant_count, bc->function_count);

    /* ── Constants pool ─────────────────────────────────────────── */
    if (bc->constant_count > 0) {
        printf("--- Constants ---\n");
        for (size_t i = 0; i < bc->constant_count; i++) {
            printf("  [%4zu] %s  %s\n",
                   i,
                   value_type_name(bc->constants[i].type),
                   value_to_string(bc->constants[i]));
        }
        printf("\n");
    }

    /* ── Function table ─────────────────────────────────────────── */
    if (bc->function_count > 0) {
        printf("--- Functions ---\n");
        for (size_t i = 0; i < bc->function_count; i++) {
            FuncEntry *fe = &bc->functions[i];
            printf("  [%4zu] %-24s  start_ip=%-6u  params=%d  locals=%d\n",
                   i,
                   fe->name ? fe->name : "?",
                   fe->start_ip,
                   fe->param_count,
                   fe->local_count);
        }
        printf("\n");
    }

    /* ── Instructions ───────────────────────────────────────────── */
    printf("--- Instructions ---\n");
    for (size_t ip = 0; ip < bc->instruction_count; ip++) {
        Instruction *ins = &bc->instructions[ip];

        /* Annotate entry points from the function table. */
        for (size_t f = 0; f < bc->function_count; f++) {
            if (bc->functions[f].start_ip == (uint32_t)ip)
                printf("\n<%s>:\n", bc->functions[f].name ? bc->functions[f].name : "?");
        }

        /* Location prefix (filename:line when available). */
        if (ins->location.filename && ins->location.line > 0)
            printf("  %4zu  %-20s  ", ip, opcode_name(ins->opcode));
        else
            printf("  %4zu  %-20s  ", ip, opcode_name(ins->opcode));

        /* Operand annotation per opcode. */
        switch (ins->opcode) {
            case OP_PUSH_CONST:
                if (ins->operand1 < (uint32_t)bc->constant_count)
                    printf("const[%u] = %s",
                           ins->operand1,
                           value_to_string(bc->constants[ins->operand1]));
                else
                    printf("const[%u] (out of range)", ins->operand1);
                break;

            case OP_LOAD_VAR:
            case OP_STORE_VAR:
                printf("local[%u]", ins->operand1);
                break;

            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL:
                printf("global[%u]", ins->operand1);
                break;

            case OP_JUMP:
                printf("-> %u", ins->operand1);
                break;

            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE:
                printf("-> %u", ins->operand1);
                break;

            case OP_CALL:
                if (ins->operand1 == 0xFFFFFFFF)
                    printf("func=unresolved  argc=%u", ins->operand2);
                else if (ins->operand1 < (uint32_t)bc->function_count)
                    printf("func=%s  argc=%u",
                           bc->functions[ins->operand1].name
                               ? bc->functions[ins->operand1].name : "?",
                           ins->operand2);
                else
                    printf("func[%u]  argc=%u", ins->operand1, ins->operand2);
                break;

            case OP_CALL_BUILTIN:
                printf("builtin_id=%u  argc=%u", ins->operand1, ins->operand2);
                break;

            case OP_ARRAY_NEW:
                printf("count=%u", ins->operand1);
                break;

            case OP_HALT:
                printf("exit_code=%u", ins->operand1);
                break;

            default:
                /* No operands worth printing for zero-operand instructions. */
                break;
        }

        /* Source location suffix. */
        if (ins->location.filename && ins->location.line > 0)
            printf("  ; %s:%d", ins->location.filename, ins->location.line);

        putchar('\n');
    }
    printf("\n=== End of Disassembly ===\n");
}
