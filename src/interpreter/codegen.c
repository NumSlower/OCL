#include "codegen.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Forward declarations */
static void codegen_node(CodeGenerator *gen, ASTNode *node);
static void codegen_expr(CodeGenerator *gen, ExprNode *expr);

/* Built-in function helpers */
static int codegen_lookup_builtin(CodeGenerator *gen, const char *name) {
    for (size_t i = 0; i < gen->builtin_count; i++) {
        if (strcmp(gen->builtins[i].name, name) == 0) {
            return gen->builtins[i].id;
        }
    }
    return -1;  /* Not found */
}

static void codegen_register_builtins(CodeGenerator *gen) {
    /* Register print function */
    if (gen->builtin_count >= gen->builtin_capacity) {
        gen->builtin_capacity = 10;
        gen->builtins = ocl_malloc(gen->builtin_capacity * sizeof(BuiltinFunction));
    }
    gen->builtins[0].name = ocl_malloc(6);
    strcpy(gen->builtins[0].name, "print");
    gen->builtins[0].id = 1;
    gen->builtin_count++;
    
    /* Register printf function */
    if (gen->builtin_count >= gen->builtin_capacity) {
        gen->builtin_capacity *= 2;
        gen->builtins = ocl_realloc(gen->builtins, gen->builtin_capacity * sizeof(BuiltinFunction));
    }
    gen->builtins[1].name = ocl_malloc(7);
    strcpy(gen->builtins[1].name, "printf");
    gen->builtins[1].id = 2;
    gen->builtin_count++;
}

/* Variable tracking helpers */
static int codegen_lookup_variable(CodeGenerator *gen, const char *name) {
    for (size_t i = 0; i < gen->variable_count; i++) {
        if (strcmp(gen->variables[i].name, name) == 0) {
            return gen->variables[i].index;
        }
    }
    return -1;  /* Not found */
}

static void codegen_add_variable(CodeGenerator *gen, const char *name, int index) {
    /* Check if already exists */
    if (codegen_lookup_variable(gen, name) >= 0) {
        return;  /* Already tracked */
    }
    
    if (gen->variable_count >= gen->variable_capacity) {
        gen->variable_capacity = gen->variable_capacity ? gen->variable_capacity * 2 : 10;
        gen->variables = ocl_realloc(gen->variables, 
                                     gen->variable_capacity * sizeof(VariableMapping));
    }
    
    gen->variables[gen->variable_count].name = ocl_malloc(strlen(name) + 1);
    strcpy(gen->variables[gen->variable_count].name, name);
    gen->variables[gen->variable_count].index = index;
    gen->variable_count++;
}

/* Generate code for an expression */
static void codegen_expr(CodeGenerator *gen, ExprNode *expr) {
    if (!expr) return;
    
    switch (expr->base.type) {
        case AST_LITERAL: {
            LiteralNode *lit = (LiteralNode *)expr;
            uint32_t const_idx = bytecode_add_constant(gen->bytecode, lit->value);
            bytecode_emit(gen->bytecode, OP_PUSH_CONST, const_idx, 0, lit->base.location);
            break;
        }
        case AST_IDENTIFIER: {
            IdentifierNode *id = (IdentifierNode *)expr;
            int var_idx = codegen_lookup_variable(gen, id->name);
            if (var_idx < 0) {
                /* Undefined variable - emit warning and load 0 */
                fprintf(stderr, "WARNING: Undefined variable '%s' at %s:%u:%u\n", 
                        id->name, id->base.location.filename, 
                        id->base.location.line, id->base.location.column);
                var_idx = 0;
            }
            bytecode_emit(gen->bytecode, OP_LOAD_VAR, var_idx, 0, id->base.location);
            break;
        }
        case AST_BIN_OP: {
            BinOpNode *binop = (BinOpNode *)expr;
            
            /* Handle assignment specially */
            if (strcmp(binop->operator, "=") == 0) {
                if (binop->left->base.type == AST_IDENTIFIER) {
                    IdentifierNode *id = (IdentifierNode *)binop->left;
                    codegen_expr(gen, binop->right);
                    int var_idx = codegen_lookup_variable(gen, id->name);
                    if (var_idx < 0) {
                        fprintf(stderr, "WARNING: Assigning to undefined variable '%s'\n", id->name);
                        var_idx = 0;
                    }
                    bytecode_emit(gen->bytecode, OP_STORE_VAR, var_idx, 0, binop->base.location);
                }
                break;
            }
            
            /* Regular binary operations */
            codegen_expr(gen, binop->left);
            codegen_expr(gen, binop->right);
            
            if (strcmp(binop->operator, "+") == 0) {
                bytecode_emit(gen->bytecode, OP_ADD, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "-") == 0) {
                bytecode_emit(gen->bytecode, OP_SUBTRACT, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "*") == 0) {
                bytecode_emit(gen->bytecode, OP_MULTIPLY, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "/") == 0) {
                bytecode_emit(gen->bytecode, OP_DIVIDE, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "%") == 0) {
                bytecode_emit(gen->bytecode, OP_MODULO, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "==") == 0) {
                bytecode_emit(gen->bytecode, OP_EQUAL, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "!=") == 0) {
                bytecode_emit(gen->bytecode, OP_NOT_EQUAL, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "<") == 0) {
                bytecode_emit(gen->bytecode, OP_LESS, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "<=") == 0) {
                bytecode_emit(gen->bytecode, OP_LESS_EQUAL, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, ">") == 0) {
                bytecode_emit(gen->bytecode, OP_GREATER, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, ">=") == 0) {
                bytecode_emit(gen->bytecode, OP_GREATER_EQUAL, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "&&") == 0) {
                bytecode_emit(gen->bytecode, OP_AND, 0, 0, binop->base.location);
            } else if (strcmp(binop->operator, "||") == 0) {
                bytecode_emit(gen->bytecode, OP_OR, 0, 0, binop->base.location);
            }
            break;
        }
        case AST_UNARY_OP: {
            UnaryOpNode *unop = (UnaryOpNode *)expr;
            codegen_expr(gen, unop->operand);
            
            if (strcmp(unop->operator, "-") == 0) {
                bytecode_emit(gen->bytecode, OP_NEGATE, 0, 0, unop->base.location);
            } else if (strcmp(unop->operator, "!") == 0) {
                bytecode_emit(gen->bytecode, OP_NOT, 0, 0, unop->base.location);
            }
            break;
        }
        case AST_CALL: {
            CallNode *call = (CallNode *)expr;
            
            /* Check if this is a built-in function */
            int builtin_id = codegen_lookup_builtin(gen, call->function_name);
            
            if (builtin_id > 0) {
                /* Built-in function - emit special CALL_BUILTIN opcode */
                /* Evaluate arguments */
                for (size_t i = 0; i < call->argument_count; i++) {
                    codegen_expr(gen, call->arguments[i]);
                }
                /* operand1 = built-in function ID, operand2 = argument count */
                bytecode_emit(gen->bytecode, OP_CALL, builtin_id, call->argument_count, call->base.location);
            } else {
                /* User-defined function */
                /* Evaluate arguments */
                for (size_t i = 0; i < call->argument_count; i++) {
                    codegen_expr(gen, call->arguments[i]);
                }
                /* TODO: Look up function offset */
                bytecode_emit(gen->bytecode, OP_CALL, 0, call->argument_count, call->base.location);
            }
            break;
        }
        default:
            break;
    }
}

/* Generate code for a statement */
static void codegen_node(CodeGenerator *gen, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
        case AST_IDENTIFIER:
        case AST_BIN_OP:
        case AST_UNARY_OP:
        case AST_CALL: {
            /* Expression statement - evaluate and pop result */
            codegen_expr(gen, (ExprNode *)node);
            bytecode_emit(gen->bytecode, OP_POP, 0, 0, node->location);
            break;
        }
        case AST_VAR_DECL: {
            VarDeclNode *var = (VarDeclNode *)node;
            int var_idx = gen->local_count++;
            codegen_add_variable(gen, var->name, var_idx);
            
            if (var->initializer) {
                codegen_expr(gen, var->initializer);
                bytecode_emit(gen->bytecode, OP_STORE_VAR, var_idx, 0, var->base.location);
            } else {
                /* Uninitialized - push null/0 */
                uint32_t const_idx = bytecode_add_constant(gen->bytecode, value_int(0));
                bytecode_emit(gen->bytecode, OP_PUSH_CONST, const_idx, 0, var->base.location);
                bytecode_emit(gen->bytecode, OP_STORE_VAR, var_idx, 0, var->base.location);
            }
            break;
        }
        case AST_FUNC_DECL: {
            FuncDeclNode *func = (FuncDeclNode *)node;
            
            /* Save variable count and reset for this function scope */
            size_t saved_var_count = gen->variable_count;
            int saved_local_count = gen->local_count;
            gen->local_count = 0;
            
            /* Register parameters as variables */
            for (size_t i = 0; i < func->param_count; i++) {
                codegen_add_variable(gen, func->params[i]->name, (int)i);
            }
            
            /* Now codegen_local_count counts params, increment for local vars in function */
            gen->local_count = (int)func->param_count;
            
            /* Generate function body */
            if (func->body) {
                for (size_t i = 0; i < func->body->statement_count; i++) {
                    codegen_node(gen, func->body->statements[i]);
                }
            }
            
            /* Restore variable tracking */
            gen->variable_count = saved_var_count;
            gen->local_count = saved_local_count;
            break;
        }
        case AST_BLOCK: {
            BlockNode *block = (BlockNode *)node;
            for (size_t i = 0; i < block->statement_count; i++) {
                codegen_node(gen, block->statements[i]);
            }
            break;
        }
        case AST_RETURN: {
            ReturnNode *ret = (ReturnNode *)node;
            if (ret->value) {
                codegen_expr(gen, ret->value);
            }
            bytecode_emit(gen->bytecode, OP_RETURN, 0, 0, ret->base.location);
            break;
        }
        case AST_IF_STMT: {
            IfStmtNode *ifstmt = (IfStmtNode *)node;
            codegen_expr(gen, ifstmt->condition);
            
            /* Jump if false */
            uint32_t jump_false_idx = gen->bytecode->instruction_count;
            bytecode_emit(gen->bytecode, OP_JUMP_IF_FALSE, 0, 0, ifstmt->base.location);
            
            /* Generate then block */
            if (ifstmt->then_block) {
                for (size_t i = 0; i < ifstmt->then_block->statement_count; i++) {
                    codegen_node(gen, ifstmt->then_block->statements[i]);
                }
            }
            
            /* Patch jump to else block or end */
            if (ifstmt->else_block) {
                uint32_t jump_end_idx = gen->bytecode->instruction_count;
                bytecode_emit(gen->bytecode, OP_JUMP, 0, 0, ifstmt->base.location);
                gen->bytecode->instructions[jump_false_idx].operand1 = gen->bytecode->instruction_count;
                
                /* Generate else block */
                for (size_t i = 0; i < ifstmt->else_block->statement_count; i++) {
                    codegen_node(gen, ifstmt->else_block->statements[i]);
                }
                
                /* Patch jump-to-end */
                gen->bytecode->instructions[jump_end_idx].operand1 = gen->bytecode->instruction_count;
            } else {
                gen->bytecode->instructions[jump_false_idx].operand1 = gen->bytecode->instruction_count;
            }
            break;
        }
        case AST_IMPORT:
            /* Ignore imports for bytecode generation */
            break;
        default:
            break;
    }
}

/* Public API */
CodeGenerator *codegen_create(ErrorCollector *errors) {
    CodeGenerator *gen = ocl_malloc(sizeof(CodeGenerator));
    gen->bytecode = NULL;
    gen->errors = errors;
    gen->local_count = 0;
    gen->scope_level = 0;
    gen->variables = NULL;
    gen->variable_count = 0;
    gen->variable_capacity = 0;
    gen->builtins = NULL;
    gen->builtin_count = 0;
    gen->builtin_capacity = 0;
    
    /* Register built-in functions */
    codegen_register_builtins(gen);
    
    return gen;
}

void codegen_free(CodeGenerator *gen) {
    if (!gen) return;
    
    for (size_t i = 0; i < gen->variable_count; i++) {
        ocl_free(gen->variables[i].name);
    }
    ocl_free(gen->variables);
    
    for (size_t i = 0; i < gen->builtin_count; i++) {
        ocl_free(gen->builtins[i].name);
    }
    ocl_free(gen->builtins);
    
    ocl_free(gen);
}

bool codegen_generate(CodeGenerator *gen, ProgramNode *program, Bytecode *output) {
    if (!gen || !program || !output) return false;
    
    gen->bytecode = output;
    gen->local_count = 0;
    gen->variable_count = 0;
    
    for (size_t i = 0; i < program->node_count; i++) {
        codegen_node(gen, program->nodes[i]);
    }
    
    /* Emit halt at program end */
    bytecode_emit(gen->bytecode, OP_HALT, 0, 0, (SourceLocation){1, 1, "program"});
    
    return true;
}
