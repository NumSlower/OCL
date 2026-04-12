#include "bytecode.h"
#include "common.h"
#include <stdio.h>
#include <string.h>

#define OCL_EXEC_SHEBANG "#!/bin/ocl.elf\n"
#define OCL_EXEC_MAGIC   "OCLBC2\n"

Bytecode *bytecode_create(void) {
    Bytecode *bc = ocl_malloc(sizeof(Bytecode));
    bc->instructions = NULL; bc->instruction_count = 0; bc->instruction_capacity = 0;
    bc->constants = NULL; bc->constant_count = 0; bc->constant_capacity = 0;
    bc->functions = NULL; bc->function_count = 0; bc->function_capacity = 0;
    return bc;
}

void bytecode_free(Bytecode *bc) {
    if (!bc) return;
    for (size_t i = 0; i < bc->instruction_count; i++)
        ocl_free((char *)bc->instructions[i].location.filename);
    ocl_free(bc->instructions);
    for (size_t i = 0; i < bc->constant_count; i++) value_free(bc->constants[i]);
    ocl_free(bc->constants);
    for (size_t i = 0; i < bc->function_count; i++) {
        ocl_free(bc->functions[i].name);
        for (size_t j = 0; j < bc->functions[i].local_name_count; j++)
            ocl_free(bc->functions[i].local_names[j]);
        ocl_free(bc->functions[i].local_names);
        for (size_t j = 0; j < bc->functions[i].capture_count; j++)
            ocl_free(bc->functions[i].captures[j].name);
        ocl_free(bc->functions[i].captures);
    }
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
    ins->opcode = op;
    ins->operand1 = op1;
    ins->operand2 = op2;
    ins->location = loc;
    ins->location.filename = loc.filename ? ocl_strdup(loc.filename) : NULL;
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
    fe->name = ocl_strdup(name);
    fe->start_ip = start_ip;
    fe->param_count = param_count;
    fe->local_count = 0;
    fe->local_names = NULL;
    fe->local_name_count = 0;
    fe->captures = NULL;
    fe->capture_count = 0;
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
        case OP_DUP:             return "DUP";
        case OP_POP:             return "POP";
        case OP_LOAD_VAR:        return "LOAD_VAR";
        case OP_STORE_VAR:       return "STORE_VAR";
        case OP_LOAD_CAPTURE:    return "LOAD_CAPTURE";
        case OP_STORE_CAPTURE:   return "STORE_CAPTURE";
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
        case OP_JUMP_IF_NOT_NULL:return "JUMP_IF_NOT_NULL";
        case OP_CALL:            return "CALL";
        case OP_CALL_VALUE:      return "CALL_VALUE";
        case OP_RETURN:          return "RETURN";
        case OP_HALT:            return "HALT";
        case OP_ARRAY_NEW:       return "ARRAY_NEW";
        case OP_ARRAY_GET:       return "ARRAY_GET";
        case OP_ARRAY_SET:       return "ARRAY_SET";
        case OP_ARRAY_LEN:       return "ARRAY_LEN";
        case OP_BIT_AND:         return "BIT_AND";
        case OP_BIT_OR:          return "BIT_OR";
        case OP_BIT_XOR:         return "BIT_XOR";
        case OP_BIT_NOT:         return "BIT_NOT";
        case OP_LSHIFT:          return "LSHIFT";
        case OP_RSHIFT:          return "RSHIFT";
        case OP_STRUCT_NEW:      return "STRUCT_NEW";
        case OP_STRUCT_GET:      return "STRUCT_GET";
        case OP_STRUCT_SET:      return "STRUCT_SET";
        case OP_MAKE_FUNCTION:   return "MAKE_FUNCTION";
        default:                 return "UNKNOWN";
    }
}

void bytecode_dump(Bytecode *bc) {
    if (!bc) return;

    printf("=== Bytecode Disassembly ===\n");
    printf("Instructions: %llu  Constants: %llu  Functions: %llu\n\n",
           (unsigned long long)bc->instruction_count,
           (unsigned long long)bc->constant_count,
           (unsigned long long)bc->function_count);

    if (bc->constant_count > 0) {
        printf("--- Constants ---\n");
        for (size_t i = 0; i < bc->constant_count; i++) {
            printf("  [%4llu] %s  %s\n",
                   (unsigned long long)i,
                   value_type_name(bc->constants[i].type),
                   value_to_string(bc->constants[i]));
        }
        printf("\n");
    }

    if (bc->function_count > 0) {
        printf("--- Functions ---\n");
        for (size_t i = 0; i < bc->function_count; i++) {
            FuncEntry *fe = &bc->functions[i];
            printf("  [%4llu] %-24s  start_ip=%-6u  params=%d  locals=%d\n",
                   (unsigned long long)i,
                   fe->name ? fe->name : "?",
                   fe->start_ip,
                   fe->param_count,
                   fe->local_count);
        }
        printf("\n");
    }

    printf("--- Instructions ---\n");
    for (size_t ip = 0; ip < bc->instruction_count; ip++) {
        Instruction *ins = &bc->instructions[ip];

        for (size_t f = 0; f < bc->function_count; f++) {
            if (bc->functions[f].start_ip == (uint32_t)ip)
                printf("\n<%s>:\n", bc->functions[f].name ? bc->functions[f].name : "?");
        }

        if (ins->location.filename && ins->location.line > 0)
            printf("  %4llu  %-20s  ", (unsigned long long)ip, opcode_name(ins->opcode));
        else
            printf("  %4llu  %-20s  ", (unsigned long long)ip, opcode_name(ins->opcode));

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
            case OP_LOAD_CAPTURE:
            case OP_STORE_CAPTURE:
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
            case OP_JUMP_IF_NOT_NULL:
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
            case OP_CALL_VALUE:
                printf("argc=%u", ins->operand2);
                break;
            case OP_ARRAY_NEW:
                printf("count=%u", ins->operand1);
                break;
            case OP_STRUCT_NEW:
                printf("type_const=%u  fields=%u", ins->operand1, ins->operand2);
                break;
            case OP_STRUCT_GET:
            case OP_STRUCT_SET:
                printf("field_const=%u", ins->operand1);
                break;
            case OP_HALT:
                printf("exit_code=%u", ins->operand1);
                break;
            case OP_MAKE_FUNCTION:
                if (ins->operand1 < (uint32_t)bc->function_count)
                    printf("func=%s", bc->functions[ins->operand1].name);
                else
                    printf("func[%u]", ins->operand1);
                break;
            default:
                break;
        }

        if (ins->location.filename && ins->location.line > 0)
            printf("  ; %s:%d", ins->location.filename, ins->location.line);

        putchar('\n');
    }
    printf("\n=== End of Disassembly ===\n");
}

static bool write_exact(FILE *f, const void *buf, size_t size) {
    if (!f) return false;
    const unsigned char *p = (const unsigned char *)buf;
    for (size_t i = 0; i < size; i++) {
        if (fputc((int)p[i], f) == EOF)
            return false;
    }
    return true;
}

static bool read_exact(FILE *f, void *buf, size_t size) {
    return f && fread(buf, 1, size, f) == size;
}

static bool write_u8(FILE *f, uint8_t value) {
    return write_exact(f, &value, sizeof(value));
}

static bool write_u32(FILE *f, uint32_t value) {
    return write_exact(f, &value, sizeof(value));
}

static bool write_i32(FILE *f, int32_t value) {
    return write_exact(f, &value, sizeof(value));
}

static bool write_i64(FILE *f, int64_t value) {
    return write_exact(f, &value, sizeof(value));
}

static bool write_f64(FILE *f, double value) {
    return write_exact(f, &value, sizeof(value));
}

static bool read_u8(FILE *f, uint8_t *value) {
    return read_exact(f, value, sizeof(*value));
}

static bool read_u32(FILE *f, uint32_t *value) {
    return read_exact(f, value, sizeof(*value));
}

static bool read_i32(FILE *f, int32_t *value) {
    return read_exact(f, value, sizeof(*value));
}

static bool read_i64(FILE *f, int64_t *value) {
    return read_exact(f, value, sizeof(*value));
}

static bool read_f64(FILE *f, double *value) {
    return read_exact(f, value, sizeof(*value));
}

static bool write_string(FILE *f, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    if (!write_u32(f, len)) return false;
    if (len == 0) return true;
    return write_exact(f, s, len);
}

static char *read_string(FILE *f) {
    uint32_t len = 0;
    if (!read_u32(f, &len)) return NULL;
    char *s = ocl_malloc((size_t)len + 1);
    if (len > 0 && !read_exact(f, s, len)) {
        ocl_free(s);
        return NULL;
    }
    s[len] = '\0';
    return s;
}

bool bytecode_write_executable(Bytecode *bc, const char *path) {
    if (!bc || !path) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    bool ok = write_exact(f, OCL_EXEC_SHEBANG, sizeof(OCL_EXEC_SHEBANG) - 1) &&
              write_exact(f, OCL_EXEC_MAGIC, sizeof(OCL_EXEC_MAGIC) - 1) &&
              write_u32(f, (uint32_t)bc->instruction_count) &&
              write_u32(f, (uint32_t)bc->constant_count) &&
              write_u32(f, (uint32_t)bc->function_count);

    for (size_t i = 0; ok && i < bc->constant_count; i++) {
        Value v = bc->constants[i];
        ok = write_u8(f, (uint8_t)v.type);
        if (!ok) break;
        switch (v.type) {
            case VALUE_INT:
                ok = write_i64(f, v.data.int_val);
                break;
            case VALUE_FLOAT:
                ok = write_f64(f, v.data.float_val);
                break;
            case VALUE_STRING:
                ok = write_string(f, v.data.string_val ? v.data.string_val : "");
                break;
            case VALUE_BOOL:
                ok = write_u8(f, v.data.bool_val ? 1u : 0u);
                break;
            case VALUE_CHAR:
                ok = write_u8(f, (uint8_t)(unsigned char)v.data.char_val);
                break;
            case VALUE_NULL:
                break;
            case VALUE_ARRAY:
            case VALUE_STRUCT:
            case VALUE_FUNCTION:
                ok = false;
                break;
        }
    }

    for (size_t i = 0; ok && i < bc->function_count; i++) {
        FuncEntry *fe = &bc->functions[i];
        ok = write_string(f, fe->name ? fe->name : "") &&
             write_u32(f, fe->start_ip) &&
             write_i32(f, fe->param_count) &&
             write_i32(f, fe->local_count) &&
             write_u32(f, (uint32_t)fe->local_name_count) &&
             write_u32(f, (uint32_t)fe->capture_count);
        for (size_t j = 0; ok && j < fe->local_name_count; j++)
            ok = write_string(f, fe->local_names[j] ? fe->local_names[j] : "");
        for (size_t j = 0; ok && j < fe->capture_count; j++) {
            ok = write_string(f, fe->captures[j].name ? fe->captures[j].name : "") &&
                 write_u32(f, (uint32_t)fe->captures[j].source) &&
                 write_u32(f, fe->captures[j].slot);
        }
    }

    for (size_t i = 0; ok && i < bc->instruction_count; i++) {
        Instruction *ins = &bc->instructions[i];
        ok = write_u32(f, (uint32_t)ins->opcode) &&
             write_u32(f, ins->operand1) &&
             write_u32(f, ins->operand2) &&
             write_i32(f, ins->location.line) &&
             write_i32(f, ins->location.column) &&
             write_string(f, ins->location.filename ? ins->location.filename : "");
    }

    if (fclose(f) != 0) ok = false;
    return ok;
}

Bytecode *bytecode_read_executable(const char *path, bool *is_executable) {
    if (is_executable) *is_executable = false;
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    char shebang[sizeof(OCL_EXEC_SHEBANG)];
    if (!read_exact(f, shebang, sizeof(OCL_EXEC_SHEBANG) - 1)) {
        fclose(f);
        return NULL;
    }
    shebang[sizeof(OCL_EXEC_SHEBANG) - 1] = '\0';
    if (strcmp(shebang, OCL_EXEC_SHEBANG) != 0) {
        fclose(f);
        return NULL;
    }

    char magic[sizeof(OCL_EXEC_MAGIC)];
    if (!read_exact(f, magic, sizeof(OCL_EXEC_MAGIC) - 1)) {
        fclose(f);
        return NULL;
    }
    magic[sizeof(OCL_EXEC_MAGIC) - 1] = '\0';
    if (strcmp(magic, OCL_EXEC_MAGIC) != 0) {
        fclose(f);
        return NULL;
    }

    if (is_executable) *is_executable = true;

    uint32_t instruction_count = 0;
    uint32_t constant_count = 0;
    uint32_t function_count = 0;
    if (!read_u32(f, &instruction_count) ||
        !read_u32(f, &constant_count) ||
        !read_u32(f, &function_count)) {
        fclose(f);
        return NULL;
    }

    Bytecode *bc = bytecode_create();
    bc->instruction_capacity = bc->instruction_count = instruction_count;
    bc->constant_capacity = bc->constant_count = constant_count;
    bc->function_capacity = bc->function_count = function_count;

    if (instruction_count > 0)
        bc->instructions = ocl_malloc((size_t)instruction_count * sizeof(Instruction));
    if (constant_count > 0)
        bc->constants = ocl_malloc((size_t)constant_count * sizeof(Value));
    if (function_count > 0)
        bc->functions = ocl_malloc((size_t)function_count * sizeof(FuncEntry));

    bool ok = true;

    for (size_t i = 0; ok && i < constant_count; i++) {
        uint8_t type = 0;
        ok = read_u8(f, &type);
        if (!ok) break;

        Value v = value_null();
        switch ((ValueType)type) {
            case VALUE_INT:
                ok = read_i64(f, &v.data.int_val);
                v.type = VALUE_INT;
                break;
            case VALUE_FLOAT:
                ok = read_f64(f, &v.data.float_val);
                v.type = VALUE_FLOAT;
                break;
            case VALUE_STRING:
                v.type = VALUE_STRING;
                v.owned = true;
                v.data.string_val = read_string(f);
                ok = v.data.string_val != NULL;
                break;
            case VALUE_BOOL: {
                uint8_t b = 0;
                ok = read_u8(f, &b);
                v.type = VALUE_BOOL;
                v.data.bool_val = b != 0;
                break;
            }
            case VALUE_CHAR: {
                uint8_t c = 0;
                ok = read_u8(f, &c);
                v.type = VALUE_CHAR;
                v.data.char_val = (char)c;
                break;
            }
            case VALUE_NULL:
                v = value_null();
                break;
            case VALUE_ARRAY:
            case VALUE_STRUCT:
            case VALUE_FUNCTION:
            default:
                ok = false;
                break;
        }
        bc->constants[i] = v;
    }

    for (size_t i = 0; ok && i < function_count; i++) {
        char *name = read_string(f);
        int32_t param_count = 0;
        int32_t local_count = 0;
        uint32_t start_ip = 0;
        uint32_t local_name_count = 0;
        uint32_t capture_count = 0;
        ok = name != NULL &&
             read_u32(f, &start_ip) &&
             read_i32(f, &param_count) &&
             read_i32(f, &local_count) &&
             read_u32(f, &local_name_count) &&
             read_u32(f, &capture_count);
        if (!ok) {
            ocl_free(name);
            break;
        }
        bc->functions[i].name = name;
        bc->functions[i].start_ip = start_ip;
        bc->functions[i].param_count = param_count;
        bc->functions[i].local_count = local_count;
        bc->functions[i].local_name_count = local_name_count;
        bc->functions[i].local_names = local_name_count > 0
            ? ocl_malloc((size_t)local_name_count * sizeof(char *))
            : NULL;
        for (uint32_t j = 0; ok && j < local_name_count; j++) {
            bc->functions[i].local_names[j] = read_string(f);
            ok = bc->functions[i].local_names[j] != NULL;
        }
        bc->functions[i].capture_count = capture_count;
        bc->functions[i].captures = capture_count > 0
            ? ocl_malloc((size_t)capture_count * sizeof(FuncCapture))
            : NULL;
        for (uint32_t j = 0; ok && j < capture_count; j++) {
            uint32_t source = 0;
            bc->functions[i].captures[j].name = read_string(f);
            ok = bc->functions[i].captures[j].name != NULL &&
                 read_u32(f, &source) &&
                 read_u32(f, &bc->functions[i].captures[j].slot);
            bc->functions[i].captures[j].source = (FuncCaptureSource)source;
        }
    }

    for (size_t i = 0; ok && i < instruction_count; i++) {
        uint32_t opcode = 0;
        int32_t line = 0;
        int32_t column = 0;
        char *filename = NULL;
        ok = read_u32(f, &opcode) &&
             read_u32(f, &bc->instructions[i].operand1) &&
             read_u32(f, &bc->instructions[i].operand2) &&
             read_i32(f, &line) &&
             read_i32(f, &column);
        if (!ok) break;
        filename = read_string(f);
        if (!filename) { ok = false; break; }
        bc->instructions[i].opcode = (Opcode)opcode;
        bc->instructions[i].location.line = line;
        bc->instructions[i].location.column = column;
        bc->instructions[i].location.filename = filename;
    }

    fclose(f);
    if (ok) return bc;

    bytecode_free(bc);
    return NULL;
}
