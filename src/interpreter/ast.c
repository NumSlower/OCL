#include "ast.h"
#include "common.h"
#include <string.h>

ASTNode *ast_create_var_decl(SourceLocation loc, char *name, TypeNode *type, ExprNode *init) {
    VarDeclNode *n = ocl_malloc(sizeof(VarDeclNode));
    n->base.type = AST_VAR_DECL; n->base.location = loc;
    n->name = name; n->type = type; n->initializer = init;
    return (ASTNode *)n;
}

ASTNode *ast_create_func_decl(SourceLocation loc, char *name, TypeNode *ret, ParamNode **params, size_t n, BlockNode *body) {
    FuncDeclNode *fd = ocl_malloc(sizeof(FuncDeclNode));
    fd->base.type = AST_FUNC_DECL; fd->base.location = loc;
    fd->name = name; fd->return_type = ret; fd->params = params; fd->param_count = n; fd->body = body;
    return (ASTNode *)fd;
}

BlockNode *ast_create_block(SourceLocation loc) {
    BlockNode *b = ocl_malloc(sizeof(BlockNode));
    b->base.type = AST_BLOCK; b->base.location = loc;
    b->statements = NULL; b->statement_count = 0;
    return b;
}

void ast_add_statement(BlockNode *block, ASTNode *stmt) {
    if (!block || !stmt) return;
    block->statements = ocl_realloc(block->statements, (block->statement_count + 1) * sizeof(ASTNode *));
    block->statements[block->statement_count++] = stmt;
}

ASTNode *ast_create_if_stmt(SourceLocation loc, ExprNode *cond, BlockNode *then_b, BlockNode *else_b) {
    IfStmtNode *n = ocl_malloc(sizeof(IfStmtNode));
    n->base.type = AST_IF_STMT; n->base.location = loc;
    n->condition = cond; n->then_block = then_b; n->else_block = else_b;
    return (ASTNode *)n;
}

ASTNode *ast_create_return(SourceLocation loc, ExprNode *value) {
    ReturnNode *n = ocl_malloc(sizeof(ReturnNode));
    n->base.type = AST_RETURN; n->base.location = loc; n->value = value;
    return (ASTNode *)n;
}

ExprNode *ast_create_binary_op(SourceLocation loc, ExprNode *left, const char *op, ExprNode *right) {
    BinOpNode *n = ocl_malloc(sizeof(BinOpNode));
    n->base.type = AST_BIN_OP; n->base.location = loc;
    n->left = left; n->right = right; n->operator = op;
    return (ExprNode *)n;
}

ExprNode *ast_create_call(SourceLocation loc, char *name, ExprNode **args, size_t arg_count) {
    CallNode *n = ocl_malloc(sizeof(CallNode));
    n->base.type = AST_CALL; n->base.location = loc;
    n->function_name = name; n->arguments = args; n->argument_count = arg_count;
    return (ExprNode *)n;
}

ExprNode *ast_create_literal(SourceLocation loc, Value value) {
    LiteralNode *n = ocl_malloc(sizeof(LiteralNode));
    n->base.type = AST_LITERAL; n->base.location = loc;
    n->value_type = value.type; n->value = value;
    return (ExprNode *)n;
}

ExprNode *ast_create_identifier(SourceLocation loc, char *name) {
    IdentifierNode *n = ocl_malloc(sizeof(IdentifierNode));
    n->base.type = AST_IDENTIFIER; n->base.location = loc; n->name = name;
    return (ExprNode *)n;
}

TypeNode *ast_create_type(BuiltinType type, int bit_width) {
    TypeNode *t = ocl_malloc(sizeof(TypeNode));
    t->type = type; t->bit_width = bit_width; t->element_type = NULL; t->is_array = false;
    return t;
}

ParamNode *ast_create_param(char *name, TypeNode *type, SourceLocation loc) {
    ParamNode *p = ocl_malloc(sizeof(ParamNode));
    p->name = name; p->type = type; p->location = loc;
    return p;
}

void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: {
            ProgramNode *p = (ProgramNode *)node;
            for (size_t i = 0; i < p->node_count; i++) ast_free(p->nodes[i]);
            ocl_free(p->nodes); break;
        }
        case AST_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode *)node;
            ocl_free(v->name); ocl_free(v->type); ast_free((ASTNode *)v->initializer); break;
        }
        case AST_FUNC_DECL: {
            FuncDeclNode *f = (FuncDeclNode *)node;
            ocl_free(f->name); ocl_free(f->return_type);
            for (size_t i = 0; i < f->param_count; i++) {
                if (f->params[i]) { ocl_free(f->params[i]->name); ocl_free(f->params[i]->type); ocl_free(f->params[i]); }
            }
            ocl_free(f->params); ast_free((ASTNode *)f->body); break;
        }
        case AST_BLOCK: {
            BlockNode *b = (BlockNode *)node;
            for (size_t i = 0; i < b->statement_count; i++) ast_free(b->statements[i]);
            ocl_free(b->statements); break;
        }
        case AST_IF_STMT: {
            IfStmtNode *s = (IfStmtNode *)node;
            ast_free((ASTNode *)s->condition); ast_free((ASTNode *)s->then_block); ast_free((ASTNode *)s->else_block); break;
        }
        case AST_FOR_LOOP:
        case AST_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            ast_free(lp->init); ast_free((ASTNode *)lp->condition); ast_free(lp->increment); ast_free((ASTNode *)lp->body); break;
        }
        case AST_RETURN: { ReturnNode *r = (ReturnNode *)node; ast_free((ASTNode *)r->value); break; }
        case AST_BIN_OP: { BinOpNode *b = (BinOpNode *)node; ast_free((ASTNode *)b->left); ast_free((ASTNode *)b->right); break; }
        case AST_UNARY_OP: { UnaryOpNode *u = (UnaryOpNode *)node; ast_free((ASTNode *)u->operand); break; }
        case AST_CALL: {
            CallNode *c = (CallNode *)node;
            ocl_free(c->function_name);
            for (size_t i = 0; i < c->argument_count; i++) ast_free((ASTNode *)c->arguments[i]);
            ocl_free(c->arguments); break;
        }
        case AST_IDENTIFIER: { IdentifierNode *id = (IdentifierNode *)node; ocl_free(id->name); break; }
        case AST_LITERAL: { LiteralNode *lit = (LiteralNode *)node; value_free(lit->value); break; }
        case AST_IMPORT: { ImportNode *imp = (ImportNode *)node; ocl_free(imp->filename); break; }
        case AST_INDEX_ACCESS: {
            ExprNode *e = (ExprNode *)node;
            ast_free((ASTNode *)e->index_access.array); ast_free((ASTNode *)e->index_access.index); break;
        }
        default: break;
    }
    ocl_free(node);
}
