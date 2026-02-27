
#include "codegen.h"
#include "ocl_stdlib.h"
#include "common.h"
#include <string.h>
#include <stdio.h>

#define BUILTIN_PRINT   1
#define BUILTIN_PRINTF  2

static int lookup_builtin(CodeGenerator *g, const char *name) {
    for (size_t i = 0; i < g->builtin_count; i++)
        if (!strcmp(g->builtins[i].name, name)) return g->builtins[i].id;
    return -1;
}

static int lookup_local(CodeGenerator *g, const char *name) {
    for (int i = (int)g->var_count - 1; i >= 0; i--)
        if (!g->vars[i].is_global && !strcmp(g->vars[i].name, name)) return g->vars[i].slot;
    return -1;
}

static int lookup_global(CodeGenerator *g, const char *name) {
    for (size_t i = 0; i < g->global_count; i++)
        if (!strcmp(g->globals[i].name, name)) return g->globals[i].slot;
    return -1;
}

static int add_local(CodeGenerator *g, const char *name) {
    int slot = g->local_stack[g->local_stack_top - 1]++;
    if (g->var_count >= g->var_cap) {
        g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
        g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
    }
    g->vars[g->var_count].name = ocl_strdup(name);
    g->vars[g->var_count].slot = slot;
    g->vars[g->var_count].scope_level = g->scope_level;
    g->vars[g->var_count].is_global = false;
    g->var_count++;
    return slot;
}

static int add_global(CodeGenerator *g, const char *name) {
    if (g->global_count >= g->global_cap) {
        g->global_cap = g->global_cap ? g->global_cap * 2 : 16;
        g->globals = ocl_realloc(g->globals, g->global_cap * sizeof(VarSlot));
    }
    int slot = (int)g->global_count;
    g->globals[g->global_count].name = ocl_strdup(name);
    g->globals[g->global_count].slot = slot;
    g->globals[g->global_count].scope_level = 0;
    g->globals[g->global_count].is_global = true;
    g->global_count++;
    return slot;
}

static void enter_scope(CodeGenerator *g) { g->scope_level++; }

static void exit_scope(CodeGenerator *g) {
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

static void emit_node(CodeGenerator *g, ASTNode *node);
static void emit_expr(CodeGenerator *g, ExprNode *expr);

static void emit_expr(CodeGenerator *g, ExprNode *expr) {
    if (!expr) return;
    Bytecode *bc = g->bytecode;
    switch (expr->base.type) {
        case AST_LITERAL: {
            LiteralNode *lit = (LiteralNode *)expr;
            uint32_t ci = bytecode_add_constant(bc, lit->value);
            bytecode_emit(bc, OP_PUSH_CONST, ci, 0, lit->base.location);
            break;
        }
        case AST_IDENTIFIER: {
            IdentifierNode *id = (IdentifierNode *)expr;
            int local = lookup_local(g, id->name);
            if (local >= 0) { bytecode_emit(bc, OP_LOAD_VAR, (uint32_t)local, 0, id->base.location); break; }
            int global = lookup_global(g, id->name);
            if (global >= 0) { bytecode_emit(bc, OP_LOAD_GLOBAL, (uint32_t)global, 0, id->base.location); break; }
            if (g->errors) error_add(g->errors, ERROR_PARSER, id->base.location, "Undefined variable '%s'", id->name);
            uint32_t ci = bytecode_add_constant(bc, value_null());
            bytecode_emit(bc, OP_PUSH_CONST, ci, 0, id->base.location);
            break;
        }
        case AST_BIN_OP: {
            BinOpNode *b = (BinOpNode *)expr;
            if (!strcmp(b->operator, "=")) {
                if (b->left && b->left->base.type == AST_IDENTIFIER) {
                    IdentifierNode *id = (IdentifierNode *)b->left;
                    emit_expr(g, b->right);
                    int local = lookup_local(g, id->name);
                    if (local >= 0) bytecode_emit(bc, OP_STORE_VAR, (uint32_t)local, 0, b->base.location);
                    else {
                        int global = lookup_global(g, id->name);
                        if (global >= 0) bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)global, 0, b->base.location);
                        else if (g->errors) error_add(g->errors, ERROR_PARSER, b->base.location, "Cannot assign to undefined '%s'", id->name);
                    }
                } else if (b->left && b->left->base.type == AST_INDEX_ACCESS) {
                    emit_expr(g, b->left->index_access.array);
                    emit_expr(g, b->left->index_access.index);
                    emit_expr(g, b->right);
                    bytecode_emit(bc, OP_ARRAY_SET, 0, 0, b->base.location);
                }
                break;
            }
            emit_expr(g, b->left);
            emit_expr(g, b->right);
            static const struct { const char *op; Opcode code; } map[] = {
                {"+",OP_ADD},{"-",OP_SUBTRACT},{"*",OP_MULTIPLY},{"/",OP_DIVIDE},{"%",OP_MODULO},
                {"==",OP_EQUAL},{"!=",OP_NOT_EQUAL},{"<",OP_LESS},{"<=",OP_LESS_EQUAL},
                {">",OP_GREATER},{">=",OP_GREATER_EQUAL},{"&&",OP_AND},{"||",OP_OR},{NULL,0}
            };
            for (int i = 0; map[i].op; i++)
                if (!strcmp(b->operator, map[i].op)) { bytecode_emit(bc, map[i].code, 0, 0, b->base.location); break; }
            break;
        }
        case AST_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode *)expr;
            emit_expr(g, u->operand);
            if (!strcmp(u->operator, "-")) bytecode_emit(bc, OP_NEGATE, 0, 0, u->base.location);
            else if (!strcmp(u->operator, "!")) bytecode_emit(bc, OP_NOT, 0, 0, u->base.location);
            break;
        }
        case AST_CALL: {
            CallNode *c = (CallNode *)expr;
            int bid = lookup_builtin(g, c->function_name);
            if (bid > 0) {
                for (size_t i = 0; i < c->argument_count; i++) emit_expr(g, c->arguments[i]);
                bytecode_emit(bc, OP_CALL_BUILTIN, (uint32_t)bid, (uint32_t)c->argument_count, c->base.location);
            } else {
                int fidx = bytecode_find_function(bc, c->function_name);
                for (size_t i = 0; i < c->argument_count; i++) emit_expr(g, c->arguments[i]);
                bytecode_emit(bc, OP_CALL, fidx >= 0 ? (uint32_t)fidx : 0xFFFFFFFF, (uint32_t)c->argument_count, c->base.location);
            }
            break;
        }
        case AST_INDEX_ACCESS: {
            ExprNode *e = (ExprNode *)expr;
            emit_expr(g, e->index_access.array);
            emit_expr(g, e->index_access.index);
            bytecode_emit(bc, OP_ARRAY_GET, 0, 0, e->base.location);
            break;
        }
        default:
            emit_node(g, (ASTNode *)expr);
            break;
    }
}

static void emit_node(CodeGenerator *g, ASTNode *node) {
    if (!node) return;
    Bytecode *bc = g->bytecode;
    switch (node->type) {
        case AST_LITERAL:
        case AST_IDENTIFIER:
        case AST_BIN_OP: {
            BinOpNode *_b = (BinOpNode *)node;
            if (node->type == AST_BIN_OP && !strcmp(_b->operator, "="))
                emit_expr(g, (ExprNode *)node);
            else {
                emit_expr(g, (ExprNode *)node);
                bytecode_emit(bc, OP_POP, 0, 0, node->location);
            }
            break;
        }
        case AST_UNARY_OP: {
            emit_expr(g, (ExprNode *)node);
            bytecode_emit(bc, OP_POP, 0, 0, node->location);
            break;
        }
        case AST_CALL: {
            emit_expr(g, (ExprNode *)node);
            bytecode_emit(bc, OP_POP, 0, 0, node->location);
            break;
        }
        case AST_IMPORT: break;
        case AST_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode *)node;
            if (g->in_global_scope) {
                int slot = lookup_global(g, v->name);
                if (slot < 0) slot = add_global(g, v->name);
                if (v->initializer) emit_expr(g, v->initializer);
                else { uint32_t ci = bytecode_add_constant(bc, value_null()); bytecode_emit(bc, OP_PUSH_CONST, ci, 0, v->base.location); }
                bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)slot, 0, v->base.location);
            } else {
                int slot = add_local(g, v->name);
                if (v->initializer) emit_expr(g, v->initializer);
                else { uint32_t ci = bytecode_add_constant(bc, value_null()); bytecode_emit(bc, OP_PUSH_CONST, ci, 0, v->base.location); }
                bytecode_emit(bc, OP_STORE_VAR, (uint32_t)slot, 0, v->base.location);
            }
            break;
        }
        case AST_FUNC_DECL: {
            FuncDeclNode *f = (FuncDeclNode *)node;
            uint32_t fidx = bytecode_add_function(bc, f->name, 0, (int)f->param_count);
            uint32_t jump_over = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP, 0, 0, f->base.location);
            uint32_t start_ip = (uint32_t)bc->instruction_count;
            bc->functions[fidx].start_ip = start_ip;
            bytecode_patch(bc, jump_over, start_ip);
            bool saved_global = g->in_global_scope;
            int saved_scope = g->scope_level;
            g->in_global_scope = false;
            g->local_stack[g->local_stack_top++] = (int)f->param_count;
            g->scope_level++;
            for (size_t i = 0; i < f->param_count; i++) {
                if (g->var_count >= g->var_cap) {
                    g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
                    g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
                }
                g->vars[g->var_count].name = ocl_strdup(f->params[i]->name);
                g->vars[g->var_count].slot = (int)i;
                g->vars[g->var_count].scope_level = g->scope_level;
                g->vars[g->var_count].is_global = false;
                g->var_count++;
            }
            if (f->body)
                for (size_t i = 0; i < f->body->statement_count; i++) emit_node(g, f->body->statements[i]);
            if (bc->instruction_count == 0 || bc->instructions[bc->instruction_count-1].opcode != OP_RETURN) {
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, f->base.location);
                bytecode_emit(bc, OP_RETURN, 0, 0, f->base.location);
            }
            bc->functions[fidx].local_count = g->local_stack[g->local_stack_top-1];
            g->local_stack_top--;
            size_t write = 0;
            for (size_t i = 0; i < g->var_count; i++) {
                if (g->vars[i].scope_level <= saved_scope) g->vars[write++] = g->vars[i];
                else ocl_free(g->vars[i].name);
            }
            g->var_count = write;
            g->scope_level = saved_scope;
            g->in_global_scope = saved_global;
            bytecode_patch(bc, jump_over, (uint32_t)bc->instruction_count);
            break;
        }
        case AST_BLOCK: {
            BlockNode *blk = (BlockNode *)node;
            enter_scope(g);
            for (size_t i = 0; i < blk->statement_count; i++) emit_node(g, blk->statements[i]);
            exit_scope(g);
            break;
        }
        case AST_IF_STMT: {
            IfStmtNode *s = (IfStmtNode *)node;
            emit_expr(g, s->condition);
            uint32_t jf_idx = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, s->base.location);
            enter_scope(g);
            if (s->then_block) for (size_t i = 0; i < s->then_block->statement_count; i++) emit_node(g, s->then_block->statements[i]);
            exit_scope(g);
            if (s->else_block) {
                uint32_t je_idx = (uint32_t)bc->instruction_count;
                bytecode_emit(bc, OP_JUMP, 0, 0, s->base.location);
                bytecode_patch(bc, jf_idx, (uint32_t)bc->instruction_count);
                enter_scope(g);
                for (size_t i = 0; i < s->else_block->statement_count; i++) emit_node(g, s->else_block->statements[i]);
                exit_scope(g);
                bytecode_patch(bc, je_idx, (uint32_t)bc->instruction_count);
            } else {
                bytecode_patch(bc, jf_idx, (uint32_t)bc->instruction_count);
            }
            break;
        }
        case AST_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            uint32_t loop_start = (uint32_t)bc->instruction_count;
            emit_expr(g, lp->condition);
            uint32_t jf_idx = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);
            enter_scope(g);
            if (lp->body) for (size_t i = 0; i < lp->body->statement_count; i++) emit_node(g, lp->body->statements[i]);
            exit_scope(g);
            bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);
            bytecode_patch(bc, jf_idx, (uint32_t)bc->instruction_count);
            break;
        }
        case AST_FOR_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            enter_scope(g);
            if (lp->init) emit_node(g, lp->init);
            uint32_t loop_start = (uint32_t)bc->instruction_count;
            uint32_t jf_idx = 0;
            if (lp->condition) {
                emit_expr(g, lp->condition);
                jf_idx = (uint32_t)bc->instruction_count;
                bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);
            }
            if (lp->body) for (size_t i = 0; i < lp->body->statement_count; i++) emit_node(g, lp->body->statements[i]);
            if (lp->increment) {
                ExprNode *inc_expr = (ExprNode *)lp->increment;
                if (inc_expr->base.type == AST_BIN_OP && !strcmp(((BinOpNode *)inc_expr)->operator, "="))
                    emit_expr(g, inc_expr);
                else { emit_expr(g, inc_expr); bytecode_emit(bc, OP_POP, 0, 0, lp->base.location); }
            }
            bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);
            if (lp->condition) bytecode_patch(bc, jf_idx, (uint32_t)bc->instruction_count);
            exit_scope(g);
            break;
        }
        case AST_RETURN: {
            ReturnNode *r = (ReturnNode *)node;
            if (r->value) emit_expr(g, r->value);
            else { uint32_t ci = bytecode_add_constant(bc, value_null()); bytecode_emit(bc, OP_PUSH_CONST, ci, 0, r->base.location); }
            bytecode_emit(bc, OP_RETURN, 0, 0, r->base.location);
            break;
        }
        case AST_BREAK:
        case AST_CONTINUE:
            break;
        default: break;
    }
}

CodeGenerator *codegen_create(ErrorCollector *errors) {
    CodeGenerator *g = ocl_malloc(sizeof(CodeGenerator));
    g->bytecode = NULL; g->errors = errors;
    g->vars = NULL; g->var_count = 0; g->var_cap = 0; g->scope_level = 0;
    g->local_stack[0] = 0; g->local_stack_top = 1; g->in_global_scope = true;
    g->globals = NULL; g->global_count = 0; g->global_cap = 0;

    size_t stdlib_count = 0;
    const StdlibEntry *tbl = stdlib_get_table(&stdlib_count);
    size_t total = 2 + stdlib_count;
    g->builtins = ocl_malloc(total * sizeof(BuiltinDesc));
    g->builtins[0] = (BuiltinDesc){"print",  BUILTIN_PRINT};
    g->builtins[1] = (BuiltinDesc){"printf", BUILTIN_PRINTF};
    g->builtin_count = 2;
    for (size_t i = 0; i < stdlib_count; i++) {
        g->builtins[g->builtin_count].name = (char *)tbl[i].name;
        g->builtins[g->builtin_count].id = tbl[i].id;
        g->builtin_count++;
    }
    return g;
}

void codegen_free(CodeGenerator *g) {
    if (!g) return;
    for (size_t i = 0; i < g->var_count; i++) ocl_free(g->vars[i].name);
    for (size_t i = 0; i < g->global_count; i++) ocl_free(g->globals[i].name);
    ocl_free(g->vars); ocl_free(g->globals);
    if (g->builtins) ocl_free(g->builtins);
    ocl_free(g);
}

bool codegen_generate(CodeGenerator *g, ProgramNode *program, Bytecode *output) {
    if (!g || !program || !output) return false;
    g->bytecode = output; g->in_global_scope = true; g->scope_level = 0;
    g->var_count = 0; g->global_count = 0; g->local_stack[0] = 0; g->local_stack_top = 1;

    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n;
            if (lookup_global(g, v->name) < 0) add_global(g, v->name);
        }
    }
    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_FUNC_DECL) {
            FuncDeclNode *f = (FuncDeclNode *)n;
            bytecode_add_function(output, f->name, 0xFFFFFFFF, (int)f->param_count);
        }
    }
    for (size_t i = 0; i < program->node_count; i++)
        if (program->nodes[i]->type == AST_FUNC_DECL) emit_node(g, program->nodes[i]);
    for (size_t i = 0; i < program->node_count; i++)
        if (program->nodes[i]->type != AST_FUNC_DECL) emit_node(g, program->nodes[i]);

    int main_idx = bytecode_find_function(output, "main");
    if (main_idx >= 0) {
        SourceLocation entry = {1, 1, "entry"};
        bytecode_emit(output, OP_CALL, (uint32_t)main_idx, 0, entry);
    }
    SourceLocation halt_loc = {1, 1, "end"};
    bytecode_emit(output, OP_HALT, 0, 0, halt_loc);
    return true;
}
