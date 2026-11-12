#include "bytecode.h"
#include <oclrt/errno.h>

#include "common.h"

Bytecode *bytecode_create(void) {
    Bytecode *bc = ocl_malloc(sizeof(Bytecode));
    bc->instructions = NULL; bc->instruction_count = 0; bc->instruction_capacity = 0;
    bc->constants = NULL; bc->constant_count = 0; bc->constant_capacity = 0;
    bc->functions = NULL; bc->function_count = 0; bc->function_capacity = 0;
    bc->files = NULL; bc->file_count = 0;
    return bc;
}

void bytecode_free(Bytecode *bc) {
    if (!bc) return;
    ocl_free(bc->instructions);
    for (size_t i = 0; i < bc->constant_count; i++) value_free(bc->constants[i]);
    ocl_free(bc->constants);
    for (size_t i = 0; i < bc->function_count; i++) ocl_free(bc->functions[i].name);
    ocl_free(bc->functions);
    for (size_t i = 0; i < bc->file_count; i++) ocl_free(bc->files[i]);
    ocl_free(bc->files);
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
            if (start_ip != 0xFFFF) bc->functions[i].start_ip = start_ip;
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

/* ── Full bytecode disassembler ──── */

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

    printf("--- Instructions ---\n");
    for (size_t ip = 0; ip < bc->instruction_count; ip++) {
        Instruction *ins = &bc->instructions[ip];

        for (size_t f = 0; f < bc->function_count; f++) {
            if (bc->functions[f].start_ip == (uint32_t)ip)
                printf("\n<%s>:\n", bc->functions[f].name ? bc->functions[f].name : "?");
        }

        if (ins->location.filename && ins->location.line > 0)
            printf("  %4zu  %-20s  ", ip, opcode_name(ins->opcode));
        else
            printf("  %4zu  %-20s  ", ip, opcode_name(ins->opcode));

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
                if (ins->operand1 == 0xFFFF)
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
                break;
        }

        if (ins->location.filename && ins->location.line > 0)
            printf("  ; %s:%d", ins->location.filename, ins->location.line);

        putchar('\n');
    }
    printf("\n=== End of Disassembly ===\n");
}

/* ════
   Compiled bytecode files (.ocx) — little-endian, version OCX_FORMAT_VERSION
   ════
   Layout:
     offset  size  field
          0     4  magic  'O','C','X','\0'
          4     4  u32    format version
          8     8  u64    instruction_count
         16     8  u64    constant_count
         24     8  u64    function_count
         32     8  u64    file-pool count (deduplicated source-file names)
         ..     .  file pool: per entry u32 length + bytes
         ..     .  instructions (20 bytes each):
                       u32 opcode, u32 operand1, u32 operand2,
                       i32 line, i32 column, u32 file_index (0xFFFFFFFF = none)
         ..     .  constants: u8 tag + payload
                       0 Int: i64   1 Float: f64 bits   2 String: u32 len + bytes
                       3 Bool: u8   4 Char: u8          5 Null: —
         ..     .  functions: u32 name_len + bytes, u32 start_ip,
                       i32 param_count, i32 local_count
   All integers are written little-endian byte-by-byte, so files are
   portable across architectures.                                         */

#define OCX_FILE_INDEX_NONE 0xFFFFFFFFu

typedef struct { uint8_t *data; size_t len, cap; bool ok; } OutBuf;

static bool ob_write(OutBuf *b, const void *src, size_t n) {
    if (!b->ok) return false;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->len + n) nc *= 2;
        uint8_t *nd = ocl_realloc(b->data, nc);
        if (!nd) { b->ok = false; return false; }
        b->data = nd; b->cap = nc;
    }
    if (n) memcpy(b->data + b->len, src, n);
    b->len += n;
    return true;
}

static bool ob_u8 (OutBuf *b, uint8_t v)  { return ob_write(b, &v, 1); }

static bool ob_u32(OutBuf *b, uint32_t v) {
    uint8_t t[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                     (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
    return ob_write(b, t, 4);
}

static bool ob_i32(OutBuf *b, int32_t v) { return ob_u32(b, (uint32_t)v); }

static bool ob_u64(OutBuf *b, uint64_t v) {
    uint8_t t[8];
    for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * i));
    return ob_write(b, t, 8);
}

static bool ob_f64(OutBuf *b, double v) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    return ob_u64(b, bits);
}

static bool ob_str(OutBuf *b, const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (!ob_u32(b, (uint32_t)n)) return false;
    return n == 0 || ob_write(b, s, n);
}

typedef struct { const uint8_t *data; size_t len, pos; bool ok; } InBuf;

static const uint8_t *rd_bytes(InBuf *r, size_t n) {
    if (!r->ok || n > r->len - r->pos) { r->ok = false; return NULL; }
    const uint8_t *p = r->data + r->pos;
    r->pos += n;
    return p;
}

static uint8_t rd_u8(InBuf *r) {
    const uint8_t *p = rd_bytes(r, 1);
    return p ? p[0] : 0;
}

static uint32_t rd_u32(InBuf *r) {
    const uint8_t *p = rd_bytes(r, 4);
    if (!p) return 0;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_i32(InBuf *r) { return (int32_t)rd_u32(r); }

static uint64_t rd_u64(InBuf *r) {
    const uint8_t *p = rd_bytes(r, 8);
    if (!p) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static double rd_f64(InBuf *r) {
    uint64_t bits = rd_u64(r);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static char *rd_str(InBuf *r) {
    uint32_t n = rd_u32(r);
    if (!r->ok) return NULL;
    const uint8_t *p = rd_bytes(r, n);
    if (!p) return NULL;
    char *s = ocl_malloc((size_t)n + 1);
    memcpy(s, p, n);
    s[n] = '\0';
    return s;
}

bool bytecode_save(const Bytecode *bc, const char *path) {
    if (!bc || !path) return false;

    OutBuf b = { NULL, 0, 0, true };

    /* ── Build the deduplicated source-file pool first ──── */
    char  **pool   = ocl_malloc((bc->instruction_count ? bc->instruction_count : 1)
                                * sizeof(char *));
    size_t  pool_n = 0;
    for (size_t i = 0; i < bc->instruction_count; i++) {
        const char *f = bc->instructions[i].location.filename;
        if (!f) continue;
        bool seen = false;
        for (size_t j = 0; j < pool_n; j++)
            if (!strcmp(pool[j], f)) { seen = true; break; }
        if (!seen) pool[pool_n++] = (char *)f;
    }
    if (pool_n > OCX_FILE_INDEX_NONE) {
        fprintf(stderr, "ocl: cannot write '%s': too many distinct source files\n", path);
        ocl_free(pool);
        return false;
    }

    /* ── Header ──── */
    uint8_t magic[4] = { 'O', 'C', 'X', 0 };
    bool ok = ob_write(&b, magic, 4);
    ok &= ob_u32(&b, OCX_FORMAT_VERSION);
    ok &= ob_u64(&b, (uint64_t)bc->instruction_count);
    ok &= ob_u64(&b, (uint64_t)bc->constant_count);
    ok &= ob_u64(&b, (uint64_t)bc->function_count);
    ok &= ob_u64(&b, (uint64_t)pool_n);

    for (size_t i = 0; i < pool_n; i++) ok &= ob_str(&b, pool[i]);

    /* ── Instructions ──── */
    for (size_t i = 0; ok && i < bc->instruction_count; i++) {
        Instruction *ins = &bc->instructions[i];
        if ((unsigned)ins->opcode > (unsigned)OP_ARRAY_LEN) {
            fprintf(stderr, "ocl: cannot write '%s': unknown opcode %d — "
                    "recompile with a matching VM (format version %u)\n",
                    path, (int)ins->opcode, OCX_FORMAT_VERSION);
            ok = false;
            break;
        }
        uint32_t file_idx = OCX_FILE_INDEX_NONE;
        if (ins->location.filename) {
            for (size_t j = 0; j < pool_n; j++)
                if (!strcmp(pool[j], ins->location.filename)) { file_idx = (uint32_t)j; break; }
        }
        ok &= ob_u32(&b, (uint32_t)ins->opcode);
        ok &= ob_u32(&b, ins->operand1);
        ok &= ob_u32(&b, ins->operand2);
        ok &= ob_i32(&b, (int32_t)ins->location.line);
        ok &= ob_i32(&b, (int32_t)ins->location.column);
        ok &= ob_u32(&b, file_idx);
    }

    /* ── Constants ──── */
    for (size_t i = 0; ok && i < bc->constant_count; i++) {
        Value v = bc->constants[i];
        switch (v.type) {
            case VALUE_INT:
                ok &= ob_u8(&b, 0) && ob_u64(&b, (uint64_t)v.data.int_val);
                break;
            case VALUE_FLOAT:
                ok &= ob_u8(&b, 1) && ob_f64(&b, v.data.float_val);
                break;
            case VALUE_STRING:
                ok &= ob_u8(&b, 2)
                  && ob_str(&b, v.data.string_val ? v.data.string_val : "");
                break;
            case VALUE_BOOL:
                ok &= ob_u8(&b, 3) && ob_u8(&b, v.data.bool_val ? 1 : 0);
                break;
            case VALUE_CHAR:
                ok &= ob_u8(&b, 4) && ob_u8(&b, (uint8_t)v.data.char_val);
                break;
            case VALUE_NULL:
                ok &= ob_u8(&b, 5);
                break;
            default:
                fprintf(stderr, "ocl: cannot write '%s': constant %zu has a "
                        "non-serialisable type (%s)\n",
                        path, i, value_type_name(v.type));
                ok = false;
                break;
        }
    }

    /* ── Functions ──── */
    for (size_t i = 0; ok && i < bc->function_count; i++) {
        FuncEntry *fe = &bc->functions[i];
        ok &= ob_str(&b, fe->name ? fe->name : "");
        ok &= ob_u32(&b, fe->start_ip);
        ok &= ob_i32(&b, (int32_t)fe->param_count);
        ok &= ob_i32(&b, (int32_t)fe->local_count);
    }

    if (ok) {
        FILE *fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "ocl: cannot open '%s' for writing: %s\n", path, strerror(errno));
            ok = false;
        } else {
            size_t written = fwrite(b.data, 1, b.len, fp);
            if (written != b.len || fclose(fp) != 0) {
                fprintf(stderr, "ocl: write error on '%s': %s\n", path, strerror(errno));
                ok = false;
            }
        }
    }

    ocl_free(b.data);
    ocl_free(pool);
    return ok;
}

Bytecode *bytecode_load(const char *path) {
    if (!path) return NULL;

    /* ── Read the whole file ──── */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ocl: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);

    uint8_t *buf = ocl_malloc((size_t)size);
    size_t   rd  = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (rd != (size_t)size) {
        fprintf(stderr, "ocl: read error on '%s'\n", path);
        ocl_free(buf);
        return NULL;
    }

    InBuf r = { buf, (size_t)size, 0, true };
    Bytecode *bc = NULL;
    bool failed = false;

    /* ── Magic + version ──── */
    const uint8_t *magic = rd_bytes(&r, 4);
    if (!r.ok || !magic || magic[0] != 'O' || magic[1] != 'C'
        || magic[2] != 'X' || magic[3] != 0) {
        fprintf(stderr, "ocl: '%s' is not an OCL compiled bytecode (.ocx) file\n", path);
        failed = true;
        goto done;
    }
    uint32_t version = rd_u32(&r);
    if (!r.ok || version != OCX_FORMAT_VERSION) {
        fprintf(stderr, "ocl: '%s' uses bytecode format version %u "
                "(this runtime supports version %u) — recompile the program\n",
                path, version, OCX_FORMAT_VERSION);
        failed = true;
        goto done;
    }

    uint64_t ins_count = rd_u64(&r);
    uint64_t con_count = rd_u64(&r);
    uint64_t fun_count = rd_u64(&r);
    uint64_t file_pool = rd_u64(&r);
    if (!r.ok || ins_count > 0x10000000ull || con_count > 0x10000000ull
        || fun_count > 0x10000000ull || file_pool > 0x10000000ull) {
        fprintf(stderr, "ocl: '%s' is corrupt (implausible section sizes)\n", path);
        failed = true;
        goto done;
    }

    bc = bytecode_create();

    /* ── Source-file pool (owned by the Bytecode) ──── */
    bc->file_count = (size_t)file_pool;
    bc->files = ocl_malloc((file_pool ? (size_t)file_pool : 1) * sizeof(char *));
    for (uint64_t i = 0; i < file_pool; i++) {
        bc->files[i] = rd_str(&r);
        if (!r.ok || !bc->files[i]) {
            fprintf(stderr, "ocl: '%s' is corrupt (truncated file pool)\n", path);
            failed = true;
            goto done;
        }
    }

    /* ── Instructions ──── */
    for (uint64_t i = 0; i < ins_count; i++) {
        uint32_t op    = rd_u32(&r);
        uint32_t op1   = rd_u32(&r);
        uint32_t op2   = rd_u32(&r);
        int32_t  line  = rd_i32(&r);
        int32_t  col   = rd_i32(&r);
        uint32_t fidx  = rd_u32(&r);
        if (!r.ok || op > (unsigned)OP_ARRAY_LEN
            || (fidx != OCX_FILE_INDEX_NONE && fidx >= (uint32_t)file_pool)) {
            fprintf(stderr, "ocl: '%s' is corrupt (bad instruction %llu) — "
                    "recompile with a matching VM\n", path, (unsigned long long)i);
            failed = true;
            goto done;
        }
        SourceLocation loc = LOC_NONE;
        loc.line     = (int)line;
        loc.column   = (int)col;
        loc.filename = (fidx == OCX_FILE_INDEX_NONE) ? NULL : bc->files[fidx];
        bytecode_emit(bc, (Opcode)op, op1, op2, loc);
    }

    /* ── Constants (strings are appended with ownership — no extra copy) ──── */
    for (uint64_t i = 0; i < con_count; i++) {
        uint8_t tag = rd_u8(&r);
        Value   v   = value_null();
        switch (tag) {
            case 0:  v = value_int((int64_t)rd_u64(&r));      break;
            case 1:  v = value_float(rd_f64(&r));             break;
            case 2: {
                char *s = rd_str(&r);
                if (!r.ok || !s) {
                    fprintf(stderr, "ocl: '%s' is corrupt (truncated string constant)\n", path);
                    failed = true;
                    goto done;
                }
                v = value_string(s);   /* takes ownership */
                break;
            }
            case 3:  v = value_bool(rd_u8(&r) != 0);          break;
            case 4:  v = value_char((char)rd_u8(&r));         break;
            case 5:  v = value_null();                        break;
            default:
                fprintf(stderr, "ocl: '%s' is corrupt (bad constant tag %u)\n", path, tag);
                failed = true;
                goto done;
        }
        OCL_GROW_ARRAY(&bc->constants, &bc->constant_capacity, bc->constant_count, Value);
        bc->constants[bc->constant_count++] = v;
    }

    /* ── Functions ──── */
    for (uint64_t i = 0; i < fun_count; i++) {
        char    *name     = rd_str(&r);
        uint32_t start_ip = rd_u32(&r);
        int32_t  params   = rd_i32(&r);
        int32_t  locals   = rd_i32(&r);
        if (!r.ok || !name) {
            fprintf(stderr, "ocl: '%s' is corrupt (truncated function table)\n", path);
            failed = true;
            goto done;
        }
        OCL_GROW_ARRAY(&bc->functions, &bc->function_capacity, bc->function_count, FuncEntry);
        FuncEntry *fe = &bc->functions[bc->function_count++];
        fe->name        = name;              /* ownership passes to the table */
        fe->start_ip    = start_ip;
        fe->param_count = (int)params;
        fe->local_count = (int)locals;
    }

    if (!r.ok) {
        fprintf(stderr, "ocl: '%s' is corrupt (truncated bytecode file)\n", path);
        failed = true;
    }

done:
    ocl_free(buf);
    if (failed) {
        bytecode_free(bc);
        return NULL;
    }
    return bc;
}
