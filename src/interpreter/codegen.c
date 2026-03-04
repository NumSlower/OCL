#include "codegen.h"
#include "ocl_stdlib.h"
#include "common.h"
#include <string.h>
#include <stdio.h>

/* IDs for print/printf, which are handled specially in the VM. */
#define BUILTIN_PRINT   1
#define BUILTIN_PRINTF  2

/* ══════════════════════════════════════════════════════════════════
   Symbol table helpers
   ══════════════════════════════════════════════════════════════════ */

/* Look up a built-in function by name; returns its ID or -1. */
static int lookup_builtin(CodeGenerator *g, const char *name) {
    for (size_t i = 0; i < g->builtin_count; i++)
        if (!strcmp(g->builtins[i].name, name)) return g->builtins[i].id;
    return -1;
}

/* Look up a local variable by name; returns its slot index or -1.
   Searches backwards so inner-scope declarations shadow outer ones. */
static int lookup_local(CodeGenerator *g, const char *name) {
    for (int i = (int)g->var_count - 1; i >= 0; i--)
        if (!strcmp(g->vars[i].name, name)) return g->vars[i].slot;
    return -1;
}

/* Look up a global variable by name; returns its slot index or -1. */
static int lookup_global(CodeGenerator *g, const char *name) {
    for (size_t i = 0; i < g->global_count; i++)
        if (!strcmp(g->globals[i].name, name)) return g->globals[i].slot;
    return -1;
}

/*
 * add_local — register a new local variable in the current function frame.
 * Assigns the next available slot from the frame's local counter.
 */
static int add_local(CodeGenerator *g, const char *name) {
    /* The top of local_stack[] tracks the next free slot for this frame. */
    int slot = g->local_stack[g->local_stack_top - 1]++;
    if (g->var_count >= g->var_cap) {
        g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
        g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
    }
    g->vars[g->var_count].name        = ocl_strdup(name);
    g->vars[g->var_count].slot        = slot;
    g->vars[g->var_count].scope_level = g->scope_level;
    g->var_count++;
    return slot;
}

/* add_global — register a new global variable and assign the next global slot. */
static int add_global(CodeGenerator *g, const char *name) {
    if (g->global_count >= g->global_cap) {
        g->global_cap = g->global_cap ? g->global_cap * 2 : 16;
        g->globals = ocl_realloc(g->globals, g->global_cap * sizeof(VarSlot));
    }
    int slot = (int)g->global_count;
    g->globals[g->global_count].name        = ocl_strdup(name);
    g->globals[g->global_count].slot        = slot;
    g->globals[g->global_count].scope_level = 0;
    g->global_count++;
    return slot;
}

/* enter_scope / exit_scope — manage lexical variable scoping. */
static void enter_scope(CodeGenerator *g) { g->scope_level++; }

static void exit_scope(CodeGenerator *g) {
    /* Remove all vars at the current level (they are going out of scope). */
    size_t write = 0;
    for (size_t i = 0; i < g->var_count; i++) {
        if (g->vars[i].scope_level < g->scope_level)
            g->vars[write++] = g->vars[i];
        else
            ocl_free(g->vars[i].name);
    }
    g->var_count = write;
    g->scope_level--;
}

/*
 * purge_vars_at_or_above_scope — free all var-name strings whose scope_level
 * is >= `min_level`, without changing g->scope_level.
 *
 * Used by the function emitter to clean up locals that were registered
 * during body emission, even after abnormal control flow (e.g. early return).
 */
static void purge_vars_at_or_above_scope(CodeGenerator *g, int min_level) {
    size_t write = 0;
    for (size_t i = 0; i < g->var_count; i++) {
        if (g->vars[i].scope_level < min_level)
            g->vars[write++] = g->vars[i];
        else
            ocl_free(g->vars[i].name);
    }
    g->var_count = write;
}

/* ══════════════════════════════════════════════════════════════════
   Loop context — break / continue backpatching
   ══════════════════════════════════════════════════════════════════ */

/* Push a new loop context onto the loop stack. */
static void loop_push(CodeGenerator *g) {
    if (g->loop_depth >= CODEGEN_MAX_LOOP_DEPTH) {
        if (g->errors)
            error_add(g->errors, ERROR_PARSER, (SourceLocation){0, 0, NULL},
                      "Loop nesting too deep");
        return;
    }
    LoopContext *ctx = &g->loop_stack[g->loop_depth++];
    ctx->continue_target = 0;
    ctx->continue_known  = false;
    ctx->break_count     = 0;
    ctx->continue_count  = 0;
}

static void loop_pop(CodeGenerator *g)           { if (g->loop_depth > 0) g->loop_depth--; }
static LoopContext *loop_current(CodeGenerator *g){ return g->loop_depth > 0 ? &g->loop_stack[g->loop_depth - 1] : NULL; }

/* Record a break/continue jump that needs to be backpatched later. */
static void loop_add_break(CodeGenerator *g, uint32_t idx) {
    LoopContext *ctx = loop_current(g);
    if (ctx && ctx->break_count < CODEGEN_MAX_BREAKS)
        ctx->breaks[ctx->break_count++].patch_idx = idx;
}

static void loop_add_continue(CodeGenerator *g, uint32_t idx) {
    LoopContext *ctx = loop_current(g);
    if (ctx && ctx->continue_count < CODEGEN_MAX_BREAKS)
        ctx->continues[ctx->continue_count++].patch_idx = idx;
}

/* Patch all pending break jumps to point to `exit_ip` (first instruction after loop). */
static void loop_patch_breaks(CodeGenerator *g, uint32_t exit_ip) {
    LoopContext *ctx = loop_current(g);
    if (!ctx) return;
    for (int i = 0; i < ctx->break_count; i++)
        bytecode_patch(g->bytecode, ctx->breaks[i].patch_idx, exit_ip);
}

/* Patch all pending continue jumps to point to `cont_ip` (loop increment/condition). */
static void loop_patch_continues(CodeGenerator *g, uint32_t cont_ip) {
    LoopContext *ctx = loop_current(g);
    if (!ctx) return;
    for (int i = 0; i < ctx->continue_count; i++)
        bytecode_patch(g->bytecode, ctx->continues[i].patch_idx, cont_ip);
}

/* ══════════════════════════════════════════════════════════════════
   Forward declarations
   ══════════════════════════════════════════════════════════════════ */

static void emit_node(CodeGenerator *g, ASTNode *node);
static void emit_expr(CodeGenerator *g, ExprNode *expr);

/* ══════════════════════════════════════════════════════════════════
   Expression code generation
   ══════════════════════════════════════════════════════════════════ */

static void emit_expr(CodeGenerator *g, ExprNode *expr) {
    if (!expr) return;
    Bytecode *bc = g->bytecode;

    switch (expr->base.type) {

    case AST_LITERAL: {
        /* Push a constant directly from the constant pool. */
        LiteralNode *lit = (LiteralNode *)expr;
        uint32_t ci = bytecode_add_constant(bc, lit->value);
        bytecode_emit(bc, OP_PUSH_CONST, ci, 0, lit->base.location);
        break;
    }

    case AST_IDENTIFIER: {
        /* Load a variable — local takes priority over global. */
        IdentifierNode *id = (IdentifierNode *)expr;
        int local = lookup_local(g, id->name);
        if (local >= 0) {
            bytecode_emit(bc, OP_LOAD_VAR, (uint32_t)local, 0, id->base.location);
            break;
        }
        int global = lookup_global(g, id->name);
        if (global >= 0) {
            bytecode_emit(bc, OP_LOAD_GLOBAL, (uint32_t)global, 0, id->base.location);
            break;
        }
        if (g->errors)
            error_add(g->errors, ERROR_PARSER, id->base.location,
                      "Undefined variable '%s'", id->name);
        /* Emit a null push so the rest of the expression can continue. */
        uint32_t ci = bytecode_add_constant(bc, value_null());
        bytecode_emit(bc, OP_PUSH_CONST, ci, 0, id->base.location);
        break;
    }

    case AST_BIN_OP: {
        BinOpNode *b = (BinOpNode *)expr;

        /* ── Assignment ──────────────────────────────────────────── */
        if (!strcmp(b->operator, "=")) {
            emit_expr(g, b->right);
            if (b->left && b->left->base.type == AST_IDENTIFIER) {
                IdentifierNode *id = (IdentifierNode *)b->left;
                int local = lookup_local(g, id->name);
                if (local >= 0)
                    bytecode_emit(bc, OP_STORE_VAR, (uint32_t)local, 0, b->base.location);
                else {
                    int global = lookup_global(g, id->name);
                    if (global >= 0)
                        bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)global, 0, b->base.location);
                    else if (g->errors)
                        error_add(g->errors, ERROR_PARSER, b->base.location,
                                  "Cannot assign to undefined '%s'", id->name);
                }
            } else if (b->left && b->left->base.type == AST_INDEX_ACCESS) {
                /* arr[i] = val — emit array, index, then SET. */
                IndexAccessNode *ia = (IndexAccessNode *)b->left;
                emit_expr(g, ia->array_expr);
                emit_expr(g, ia->index_expr);
                bytecode_emit(bc, OP_ARRAY_SET, 0, 0, b->base.location);
            } else if (g->errors) {
                error_add(g->errors, ERROR_PARSER, b->base.location,
                          "Invalid assignment target");
            }
            break;
        }

        /* ── Short-circuit &&  ───────────────────────────────────────
         *
         * Emits:
         *   <left>
         *   JUMP_IF_FALSE → sc_false
         *   <right>
         *   JUMP_IF_FALSE → sc_false
         *   PUSH true
         *   JUMP → end
         * sc_false:
         *   PUSH false
         * end:
         */
        if (!strcmp(b->operator, "&&")) {
            emit_expr(g, b->left);
            uint32_t jf1 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, b->base.location);

            emit_expr(g, b->right);
            uint32_t jf2 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, b->base.location);

            uint32_t ci_true = bytecode_add_constant(bc, value_bool(true));
            bytecode_emit(bc, OP_PUSH_CONST, ci_true, 0, b->base.location);
            uint32_t jmp_end = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP, 0, 0, b->base.location);

            uint32_t sc_false = (uint32_t)bc->instruction_count;
            bytecode_patch(bc, jf1, sc_false);
            bytecode_patch(bc, jf2, sc_false);
            uint32_t ci_false = bytecode_add_constant(bc, value_bool(false));
            bytecode_emit(bc, OP_PUSH_CONST, ci_false, 0, b->base.location);

            bytecode_patch(bc, jmp_end, (uint32_t)bc->instruction_count);
            break;
        }

        /* ── Short-circuit ||  ───────────────────────────────────────
         *
         * Emits:
         *   <left>
         *   JUMP_IF_TRUE → sc_true
         *   <right>
         *   JUMP_IF_TRUE → sc_true
         *   PUSH false
         *   JUMP → end
         * sc_true:
         *   PUSH true
         * end:
         */
        if (!strcmp(b->operator, "||")) {
            emit_expr(g, b->left);
            uint32_t jt1 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_TRUE, 0, 0, b->base.location);

            emit_expr(g, b->right);
            uint32_t jt2 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_TRUE, 0, 0, b->base.location);

            uint32_t ci_false = bytecode_add_constant(bc, value_bool(false));
            bytecode_emit(bc, OP_PUSH_CONST, ci_false, 0, b->base.location);
            uint32_t jmp_end = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP, 0, 0, b->base.location);

            uint32_t sc_true = (uint32_t)bc->instruction_count;
            bytecode_patch(bc, jt1, sc_true);
            bytecode_patch(bc, jt2, sc_true);
            uint32_t ci_true = bytecode_add_constant(bc, value_bool(true));
            bytecode_emit(bc, OP_PUSH_CONST, ci_true, 0, b->base.location);

            bytecode_patch(bc, jmp_end, (uint32_t)bc->instruction_count);
            break;
        }

        /* ── All other binary operators ──────────────────────────── */
        emit_expr(g, b->left);
        emit_expr(g, b->right);
        static const struct { const char *op; Opcode code; } map[] = {
            {"+",  OP_ADD},       {"-",  OP_SUBTRACT},
            {"*",  OP_MULTIPLY},  {"/",  OP_DIVIDE},
            {"%",  OP_MODULO},    {"==", OP_EQUAL},
            {"!=", OP_NOT_EQUAL}, {"<",  OP_LESS},
            {"<=", OP_LESS_EQUAL},{">" , OP_GREATER},
            {">=", OP_GREATER_EQUAL},
            {NULL, 0}
        };
        for (int i = 0; map[i].op; i++)
            if (!strcmp(b->operator, map[i].op)) {
                bytecode_emit(bc, map[i].code, 0, 0, b->base.location);
                break;
            }
        break;
    }

    case AST_UNARY_OP: {
        UnaryOpNode *u = (UnaryOpNode *)expr;
        emit_expr(g, u->operand);
        if (!strcmp(u->operator, "-"))      bytecode_emit(bc, OP_NEGATE, 0, 0, u->base.location);
        else if (!strcmp(u->operator, "!")) bytecode_emit(bc, OP_NOT,    0, 0, u->base.location);
        break;
    }

    case AST_CALL: {
        /*
         * Built-ins are resolved by name and dispatched via OP_CALL_BUILTIN.
         * User functions use OP_CALL with a function-table index.
         */
        CallNode *c = (CallNode *)expr;
        int bid = lookup_builtin(g, c->function_name);
        if (bid > 0) {
            for (size_t i = 0; i < c->argument_count; i++)
                emit_expr(g, c->arguments[i]);
            bytecode_emit(bc, OP_CALL_BUILTIN,
                          (uint32_t)bid, (uint32_t)c->argument_count,
                          c->base.location);
        } else {
            int fidx = bytecode_find_function(bc, c->function_name);
            for (size_t i = 0; i < c->argument_count; i++)
                emit_expr(g, c->arguments[i]);
            /* 0xFFFFFFFF = unresolved forward reference (will produce a runtime error). */
            bytecode_emit(bc, OP_CALL,
                          fidx >= 0 ? (uint32_t)fidx : 0xFFFFFFFF,
                          (uint32_t)c->argument_count,
                          c->base.location);
        }
        break;
    }

    case AST_ARRAY_LITERAL: {
        /* Push each element then OP_ARRAY_NEW to collect them. */
        ArrayLiteralNode *al = (ArrayLiteralNode *)expr;
        for (size_t i = 0; i < al->element_count; i++)
            emit_expr(g, al->elements[i]);
        bytecode_emit(bc, OP_ARRAY_NEW, (uint32_t)al->element_count, 0, al->base.location);
        break;
    }

    case AST_INDEX_ACCESS: {
        /* Push array/string then index, then OP_ARRAY_GET. */
        IndexAccessNode *ia = (IndexAccessNode *)expr;
        emit_expr(g, ia->array_expr);
        emit_expr(g, ia->index_expr);
        bytecode_emit(bc, OP_ARRAY_GET, 0, 0, ia->base.location);
        break;
    }

    default:
        /* Fallback: treat the node as a statement-level expression. */
        emit_node(g, (ASTNode *)expr);
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
   Statement code generation
   ══════════════════════════════════════════════════════════════════ */

static void emit_node(CodeGenerator *g, ASTNode *node) {
    if (!node) return;
    Bytecode *bc = g->bytecode;

    switch (node->type) {

    /* Expression-statement forms: evaluate the expression then discard the result. */
    case AST_LITERAL:
    case AST_IDENTIFIER:
    case AST_ARRAY_LITERAL:
    case AST_INDEX_ACCESS:
    case AST_UNARY_OP:
        emit_expr(g, (ExprNode *)node);
        bytecode_emit(bc, OP_POP, 0, 0, node->location);
        break;

    case AST_BIN_OP: {
        BinOpNode *_b = (BinOpNode *)node;
        if (!strcmp(_b->operator, "=")) {
            /* Assignment as a statement — result is intentionally discarded. */
            emit_expr(g, (ExprNode *)node);
        } else {
            emit_expr(g, (ExprNode *)node);
            bytecode_emit(bc, OP_POP, 0, 0, node->location);
        }
        break;
    }

    case AST_CALL:
        emit_expr(g, (ExprNode *)node);
        bytecode_emit(bc, OP_POP, 0, 0, node->location);
        break;

    case AST_IMPORT:
        /* Imports are resolved at parse time; nothing to emit. */
        break;

    case AST_DECLARE: {
        /*
         * `declare name : Type` — forward-declare a variable to the type checker
         * without a user-supplied initialiser.  Emits null into the slot.
         */
        DeclareNode *d = (DeclareNode *)node;
        if (g->in_global_scope) {
            if (lookup_global(g, d->name) < 0) {
                int slot = add_global(g, d->name);
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, d->base.location);
                bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)slot, 0, d->base.location);
            }
        } else {
            if (lookup_local(g, d->name) < 0) {
                int slot = add_local(g, d->name);
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, d->base.location);
                bytecode_emit(bc, OP_STORE_VAR, (uint32_t)slot, 0, d->base.location);
            }
        }
        break;
    }

    case AST_VAR_DECL: {
        VarDeclNode *v = (VarDeclNode *)node;
        if (g->in_global_scope) {
            /* Global variable: find or create a global slot. */
            int slot = lookup_global(g, v->name);
            if (slot < 0) slot = add_global(g, v->name);
            if (v->initializer)
                emit_expr(g, v->initializer);
            else {
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, v->base.location);
            }
            bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)slot, 0, v->base.location);
        } else {
            /* Local variable: allocate the next slot in the current frame. */
            int slot = add_local(g, v->name);
            if (v->initializer)
                emit_expr(g, v->initializer);
            else {
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, v->base.location);
            }
            bytecode_emit(bc, OP_STORE_VAR, (uint32_t)slot, 0, v->base.location);
        }
        break;
    }

    case AST_FUNC_DECL: {
        /*
         * Function body emission strategy:
         *
         * 1. Register the function in the function table (pre-pass already did this,
         *    but bytecode_add_function is idempotent for the same name).
         * 2. Emit OP_JUMP over the body so that falling through at runtime skips it.
         * 3. Emit the body instructions.
         * 4. Append an implicit `return null` if the body doesn't end with OP_RETURN.
         * 5. Backpatch the OP_JUMP from step 2 to land after the body.
         * 6. Restore the caller's code-generation state.
         */
        FuncDeclNode *f = (FuncDeclNode *)node;
        uint32_t fidx = bytecode_add_function(bc, f->name, 0, (int)f->param_count);

        uint32_t jump_over = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, 0, 0, f->base.location);
        uint32_t start_ip = (uint32_t)bc->instruction_count;
        bc->functions[fidx].start_ip = start_ip;

        /* Save caller code-gen state. */
        bool saved_global = g->in_global_scope;
        int  saved_scope  = g->scope_level;

        g->in_global_scope = false;
        /* Push a new frame-local counter initialised to param_count
           (params occupy slots 0..param_count-1). */
        g->local_stack[g->local_stack_top++] = (int)f->param_count;
        g->scope_level++;

        /* Register parameter names as local slots 0..param_count-1. */
        for (size_t i = 0; i < f->param_count; i++) {
            if (g->var_count >= g->var_cap) {
                g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
                g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
            }
            g->vars[g->var_count].name        = ocl_strdup(f->params[i]->name);
            g->vars[g->var_count].slot        = (int)i;
            g->vars[g->var_count].scope_level = g->scope_level;
            g->var_count++;
        }

        /* Emit function body statements. */
        if (f->body)
            for (size_t i = 0; i < f->body->statement_count; i++)
                emit_node(g, f->body->statements[i]);

        /* Implicit `return null` if the body doesn't already end with OP_RETURN. */
        if (bc->instruction_count == 0 ||
            bc->instructions[bc->instruction_count - 1].opcode != OP_RETURN) {
            uint32_t ci = bytecode_add_constant(bc, value_null());
            bytecode_emit(bc, OP_PUSH_CONST, ci, 0, f->base.location);
            bytecode_emit(bc, OP_RETURN, 0, 0, f->base.location);
        }

        /* Record how many local slots this function actually used. */
        bc->functions[fidx].local_count = g->local_stack[g->local_stack_top - 1];

        /*
         * Restore caller state. purge_vars_at_or_above_scope() frees the
         * name strings for every variable that was registered inside this
         * function, including any that weren't cleaned up by exit_scope()
         * due to early returns or other abnormal control flow.
         */
        purge_vars_at_or_above_scope(g, saved_scope + 1);

        g->local_stack_top--;
        g->scope_level     = saved_scope;
        g->in_global_scope = saved_global;

        /* Patch the jump-over instruction to land after the function body. */
        bytecode_patch(bc, jump_over, (uint32_t)bc->instruction_count);
        break;
    }

    case AST_BLOCK: {
        BlockNode *blk = (BlockNode *)node;
        enter_scope(g);
        for (size_t i = 0; i < blk->statement_count; i++)
            emit_node(g, blk->statements[i]);
        exit_scope(g);
        break;
    }

    case AST_IF_STMT: {
        /*
         * if/else-if/else chain.
         *
         * Each branch:
         *   <condition>
         *   JUMP_IF_FALSE → next_branch_or_end
         *   <then body>
         *   JUMP → end               ← exit_jumps[] entry (backpatched later)
         * next_branch_or_end:
         *   ...
         * end:
         */
        uint32_t exit_jumps[256];
        int      exit_jump_count = 0;

        ASTNode *cur = node;
        while (cur && cur->type == AST_IF_STMT) {
            IfStmtNode *s = (IfStmtNode *)cur;

            emit_expr(g, s->condition);
            uint32_t jf_idx = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, s->base.location);

            enter_scope(g);
            if (s->then_block)
                for (size_t i = 0; i < s->then_block->statement_count; i++)
                    emit_node(g, s->then_block->statements[i]);
            exit_scope(g);

            /* Jump past all remaining branches (backpatched to `end`). */
            if (exit_jump_count < 256) {
                uint32_t je_idx = (uint32_t)bc->instruction_count;
                bytecode_emit(bc, OP_JUMP, 0, 0, s->base.location);
                exit_jumps[exit_jump_count++] = je_idx;
            }

            /* Patch the JUMP_IF_FALSE to land at the next branch/else/end. */
            bytecode_patch(bc, jf_idx, (uint32_t)bc->instruction_count);
            cur = s->else_next;
        }

        /* Optional plain `else { ... }` block. */
        if (cur && cur->type == AST_BLOCK) {
            enter_scope(g);
            BlockNode *eb = (BlockNode *)cur;
            for (size_t i = 0; i < eb->statement_count; i++)
                emit_node(g, eb->statements[i]);
            exit_scope(g);
        }

        /* Patch all `JUMP → end` instructions now that we know where `end` is. */
        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        for (int i = 0; i < exit_jump_count; i++)
            bytecode_patch(bc, exit_jumps[i], exit_ip);
        break;
    }

    case AST_WHILE_LOOP: {
        /*
         * while (cond) { body }
         *
         * loop_start:
         *   <condition>
         *   JUMP_IF_FALSE → exit
         *   <body>
         * continue_ip:
         *   JUMP → loop_start
         * exit:
         */
        LoopNode *lp = (LoopNode *)node;
        loop_push(g);

        uint32_t loop_start = (uint32_t)bc->instruction_count;
        emit_expr(g, lp->condition);
        uint32_t jf_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);

        enter_scope(g);
        if (lp->body)
            for (size_t i = 0; i < lp->body->statement_count; i++)
                emit_node(g, lp->body->statements[i]);
        exit_scope(g);

        /* `continue` jumps here — the backwards jump that re-tests the condition. */
        uint32_t continue_ip = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);

        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        bytecode_patch(bc, jf_idx, exit_ip);
        loop_patch_breaks(g, exit_ip);
        loop_patch_continues(g, continue_ip);
        loop_pop(g);
        break;
    }

    case AST_FOR_LOOP: {
        /*
         * for (init; cond; incr) { body }
         *
         *   <init>
         * loop_start:
         *   [<condition> + JUMP_IF_FALSE → exit]   (omitted if no condition)
         *   <body>
         * continue_ip:
         *   <increment>
         *   JUMP → loop_start
         * exit:
         */
        LoopNode *lp = (LoopNode *)node;
        loop_push(g);
        enter_scope(g); /* the init variable lives in this scope */

        if (lp->init) emit_node(g, lp->init);

        uint32_t loop_start = (uint32_t)bc->instruction_count;
        uint32_t jf_idx     = 0;
        bool     has_cond   = (lp->condition != NULL);
        if (has_cond) {
            emit_expr(g, lp->condition);
            jf_idx = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);
        }

        if (lp->body)
            for (size_t i = 0; i < lp->body->statement_count; i++)
                emit_node(g, lp->body->statements[i]);

        /* `continue` lands here so the increment still runs. */
        uint32_t continue_ip = (uint32_t)bc->instruction_count;
        if (lp->increment) {
            ExprNode *inc_expr = (ExprNode *)lp->increment;
            if (inc_expr->base.type == AST_BIN_OP &&
                !strcmp(((BinOpNode *)inc_expr)->operator, "=")) {
                /* Assignment — result left on stack, caller discards. */
                emit_expr(g, inc_expr);
            } else {
                emit_expr(g, inc_expr);
                bytecode_emit(bc, OP_POP, 0, 0, lp->base.location);
            }
        }

        bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);

        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        if (has_cond) bytecode_patch(bc, jf_idx, exit_ip);
        loop_patch_breaks(g, exit_ip);
        loop_patch_continues(g, continue_ip);
        loop_pop(g);
        exit_scope(g);
        break;
    }

    case AST_RETURN: {
        ReturnNode *r = (ReturnNode *)node;
        if (r->value)
            emit_expr(g, r->value);
        else {
            /* Implicit `return null` — void functions still push a value. */
            uint32_t ci = bytecode_add_constant(bc, value_null());
            bytecode_emit(bc, OP_PUSH_CONST, ci, 0, r->base.location);
        }
        bytecode_emit(bc, OP_RETURN, 0, 0, r->base.location);
        break;
    }

    case AST_BREAK: {
        if (!loop_current(g)) {
            if (g->errors)
                error_add(g->errors, ERROR_PARSER, node->location,
                          "'break' used outside of a loop");
            break;
        }
        /* Emit a forward jump; target is backpatched by loop_patch_breaks(). */
        uint32_t patch_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, 0, 0, node->location);
        loop_add_break(g, patch_idx);
        break;
    }

    case AST_CONTINUE: {
        if (!loop_current(g)) {
            if (g->errors)
                error_add(g->errors, ERROR_PARSER, node->location,
                          "'continue' used outside of a loop");
            break;
        }
        /* Emit a forward jump; target is backpatched by loop_patch_continues(). */
        uint32_t patch_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, 0, 0, node->location);
        loop_add_continue(g, patch_idx);
        break;
    }

    default:
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════ */

CodeGenerator *codegen_create(ErrorCollector *errors) {
    CodeGenerator *g = ocl_malloc(sizeof(CodeGenerator));
    g->bytecode        = NULL;
    g->errors          = errors;
    g->vars            = NULL;
    g->var_count       = 0;
    g->var_cap         = 0;
    g->scope_level     = 0;
    g->local_stack[0]  = 0;
    g->local_stack_top = 1;
    g->in_global_scope = true;
    g->globals         = NULL;
    g->global_count    = 0;
    g->global_cap      = 0;
    g->loop_depth      = 0;

    /* Build the builtin name→id table from the stdlib registry. */
    size_t             stdlib_count = 0;
    const StdlibEntry *tbl          = stdlib_get_table(&stdlib_count);
    size_t             total        = 2 + stdlib_count;
    g->builtins = ocl_malloc(total * sizeof(BuiltinDesc));
    /* print and printf are handled specially in the VM, so register them first. */
    g->builtins[0]     = (BuiltinDesc){"print",  BUILTIN_PRINT};
    g->builtins[1]     = (BuiltinDesc){"printf", BUILTIN_PRINTF};
    g->builtin_count   = 2;
    for (size_t i = 0; i < stdlib_count; i++) {
        g->builtins[g->builtin_count].name = (char *)tbl[i].name;
        g->builtins[g->builtin_count].id   = tbl[i].id;
        g->builtin_count++;
    }
    return g;
}

void codegen_free(CodeGenerator *g) {
    if (!g) return;
    /* Free any var-name strings still alive (e.g. after a failed generation). */
    for (size_t i = 0; i < g->var_count;   i++) ocl_free(g->vars[i].name);
    for (size_t i = 0; i < g->global_count; i++) ocl_free(g->globals[i].name);
    ocl_free(g->vars);
    ocl_free(g->globals);
    /* Builtin name pointers at [0] and [1] are string literals;
       entries [2..] borrow pointers from the stdlib table.
       Neither set is heap-owned, so only free the array itself. */
    ocl_free(g->builtins);
    ocl_free(g);
}

bool codegen_generate(CodeGenerator *g, ProgramNode *program, Bytecode *output) {
    if (!g || !program || !output) return false;

    g->bytecode        = output;
    g->in_global_scope = true;
    g->scope_level     = 0;
    g->var_count       = 0;
    g->global_count    = 0;
    g->local_stack[0]  = 0;
    g->local_stack_top = 1;
    g->loop_depth      = 0;

    /* Pass 1: pre-register all top-level global variable names so that
       forward references inside function bodies resolve correctly. */
    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n;
            if (lookup_global(g, v->name) < 0) add_global(g, v->name);
        } else if (n->type == AST_DECLARE) {
            DeclareNode *d = (DeclareNode *)n;
            if (lookup_global(g, d->name) < 0) add_global(g, d->name);
        }
    }

    /* Pass 1b: pre-register all function signatures for forward calls. */
    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_FUNC_DECL) {
            FuncDeclNode *f = (FuncDeclNode *)n;
            bytecode_add_function(output, f->name, 0xFFFFFFFF, (int)f->param_count);
        }
    }

    /* Pass 2: emit function bodies (they jump over themselves at runtime). */
    for (size_t i = 0; i < program->node_count; i++)
        if (program->nodes[i]->type == AST_FUNC_DECL)
            emit_node(g, program->nodes[i]);

    /* Pass 3: emit top-level statements (globals and the main program flow). */
    for (size_t i = 0; i < program->node_count; i++)
        if (program->nodes[i]->type != AST_FUNC_DECL)
            emit_node(g, program->nodes[i]);

    /* If `main()` is defined, call it automatically after global initialisers. */
    int main_idx = bytecode_find_function(output, "main");
    if (main_idx >= 0) {
        SourceLocation entry = {1, 1, "entry"};
        bytecode_emit(output, OP_CALL, (uint32_t)main_idx, 0, entry);
    }

    /* OP_HALT ends execution and propagates the exit code. */
    SourceLocation halt_loc = {1, 1, "end"};
    bytecode_emit(output, OP_HALT, 0, 0, halt_loc);
    return true;
}