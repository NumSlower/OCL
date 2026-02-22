#include "ast.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

/* AST Node creation utilities */
ASTNode *ast_create_var_decl(SourceLocation loc, char *name, TypeNode *type, ExprNode *initializer) {
    VarDeclNode *node = ocl_malloc(sizeof(VarDeclNode));
    node->base.type = AST_VAR_DECL;
    node->base.location = loc;
    node->name = name;
    node->type = type;
    node->initializer = initializer;
    return (ASTNode *)node;
}

ASTNode *ast_create_func_decl(SourceLocation loc, char *name, TypeNode *return_type, ParamNode **params, size_t param_count, BlockNode *body) {
    FuncDeclNode *node = ocl_malloc(sizeof(FuncDeclNode));
    node->base.type = AST_FUNC_DECL;
    node->base.location = loc;
    node->name = name;
    node->return_type = return_type;
    node->params = params;
    node->param_count = param_count;
    node->body = body;
    return (ASTNode *)node;
}

BlockNode *ast_create_block(SourceLocation loc) {
    BlockNode *node = ocl_malloc(sizeof(BlockNode));
    node->base.type = AST_BLOCK;
    node->base.location = loc;
    node->statements = NULL;
    node->statement_count = 0;
    return node;
}

void ast_add_statement(BlockNode *block, ASTNode *stmt) {
    if (!block || !stmt) return;
    
    block->statements = ocl_realloc(block->statements, (block->statement_count + 1) * sizeof(ASTNode *));
    block->statements[block->statement_count++] = stmt;
}

ASTNode *ast_create_if_stmt(SourceLocation loc, ExprNode *cond, BlockNode *then_block, BlockNode *else_block) {
    IfStmtNode *node = ocl_malloc(sizeof(IfStmtNode));
    node->base.type = AST_IF_STMT;
    node->base.location = loc;
    node->condition = cond;
    node->then_block = then_block;
    node->else_block = else_block;
    return (ASTNode *)node;
}

ASTNode *ast_create_return(SourceLocation loc, ExprNode *value) {
    ReturnNode *node = ocl_malloc(sizeof(ReturnNode));
    node->base.type = AST_RETURN;
    node->base.location = loc;
    node->value = value;
    return (ASTNode *)node;
}

ExprNode *ast_create_binary_op(SourceLocation loc, ExprNode *left, const char *op, ExprNode *right) {
    BinOpNode *node = ocl_malloc(sizeof(BinOpNode));
    node->base.type = AST_BIN_OP;
    node->base.location = loc;
    node->left = left;
    node->right = right;
    node->operator = op;
    return (ExprNode *)node;
}

ExprNode *ast_create_call(SourceLocation loc, char *name, ExprNode **args, size_t arg_count) {
    CallNode *node = ocl_malloc(sizeof(CallNode));
    node->base.type = AST_CALL;
    node->base.location = loc;
    node->function_name = name;
    node->arguments = args;
    node->argument_count = arg_count;
    return (ExprNode *)node;
}

ExprNode *ast_create_literal(SourceLocation loc, Value value) {
    LiteralNode *node = ocl_malloc(sizeof(LiteralNode));
    node->base.type = AST_LITERAL;
    node->base.location = loc;
    node->value_type = value.type;
    node->value = value;
    return (ExprNode *)node;
}

ExprNode *ast_create_identifier(SourceLocation loc, char *name) {
    IdentifierNode *node = ocl_malloc(sizeof(IdentifierNode));
    node->base.type = AST_IDENTIFIER;
    node->base.location = loc;
    node->name = name;
    return (ExprNode *)node;
}

TypeNode *ast_create_type(BuiltinType type, int bit_width) {
    TypeNode *node = ocl_malloc(sizeof(TypeNode));
    node->type = type;
    node->bit_width = bit_width;
    node->element_type = NULL;
    node->is_array = false;
    return node;
}

ParamNode *ast_create_param(char *name, TypeNode *type, SourceLocation loc) {
    ParamNode *node = ocl_malloc(sizeof(ParamNode));
    node->name = name;
    node->type = type;
    node->location = loc;
    return node;
}

/* Recursive AST freeing */
void ast_free(ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_VAR_DECL: {
            VarDeclNode *n = (VarDeclNode *)node;
            ocl_free(n->name);
            if (n->type) ocl_free(n->type);
            if (n->initializer) ast_free((ASTNode *)n->initializer);
            break;
        }
        case AST_FUNC_DECL: {
            FuncDeclNode *n = (FuncDeclNode *)node;
            ocl_free(n->name);
            if (n->return_type) ocl_free(n->return_type);
            for (size_t i = 0; i < n->param_count; i++) {
                if (n->params[i]) {
                    ocl_free(n->params[i]->name);
                    ocl_free(n->params[i]->type);
                    ocl_free(n->params[i]);
                }
            }
            ocl_free(n->params);
            if (n->body) ast_free((ASTNode *)n->body);
            break;
        }
        case AST_BLOCK: {
            BlockNode *n = (BlockNode *)node;
            for (size_t i = 0; i < n->statement_count; i++) {
                ast_free(n->statements[i]);
            }
            ocl_free(n->statements);
            break;
        }
        case AST_IF_STMT: {
            IfStmtNode *n = (IfStmtNode *)node;
            ast_free((ASTNode *)n->condition);
            ast_free((ASTNode *)n->then_block);
            if (n->else_block) ast_free((ASTNode *)n->else_block);
            break;
        }
        case AST_FOR_LOOP:
        case AST_WHILE_LOOP: {
            LoopNode *n = (LoopNode *)node;
            if (n->init) ast_free(n->init);
            if (n->condition) ast_free((ASTNode *)n->condition);
            if (n->increment) ast_free(n->increment);
            if (n->body) ast_free((ASTNode *)n->body);
            break;
        }
        case AST_RETURN: {
            ReturnNode *n = (ReturnNode *)node;
            if (n->value) ast_free((ASTNode *)n->value);
            break;
        }
        case AST_BIN_OP: {
            BinOpNode *n = (BinOpNode *)node;
            ast_free((ASTNode *)n->left);
            ast_free((ASTNode *)n->right);
            break;
        }
        case AST_CALL: {
            CallNode *n = (CallNode *)node;
            ocl_free(n->function_name);
            for (size_t i = 0; i < n->argument_count; i++) {
                ast_free((ASTNode *)n->arguments[i]);
            }
            ocl_free(n->arguments);
            break;
        }
        case AST_IDENTIFIER: {
            IdentifierNode *n = (IdentifierNode *)node;
            ocl_free(n->name);
            break;
        }
        case AST_UNARY_OP: {
            UnaryOpNode *n = (UnaryOpNode *)node;
            ast_free((ASTNode *)n->operand);
            break;
        }
        case AST_LITERAL: {
            LiteralNode *n = (LiteralNode *)node;
            value_free(n->value);
            break;
        }
        default:
            break;
    }
    
    ocl_free(node);
}
