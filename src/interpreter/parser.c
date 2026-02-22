#include "parser.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parser helper functions */
static bool parser_is_at_end(Parser *parser) {
    return parser->current >= parser->token_count || 
           parser->tokens[parser->current].type == TOKEN_EOF;
}

static Token parser_current(Parser *parser) {
    if (parser->current < parser->token_count) {
        return parser->tokens[parser->current];
    }
    return parser->tokens[parser->token_count - 1];  /* Return EOF */
}

static Token parser_previous(Parser *parser) {
    if (parser->current > 0) {
        return parser->tokens[parser->current - 1];
    }
    return parser->tokens[0];
}

static void parser_advance(Parser *parser) {
    if (!parser_is_at_end(parser)) {
        parser->current++;
    }
}

static bool parser_check(Parser *parser, TokenType type) {
    if (parser_is_at_end(parser)) return false;
    return parser_current(parser).type == type;
}

static bool parser_match(Parser *parser, TokenType type) {
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

static Token parser_consume(Parser *parser, TokenType type, const char *message) {
    if (parser_check(parser, type)) {
        Token token = parser_current(parser);
        parser_advance(parser);
        return token;
    }
    
    Token current = parser_current(parser);
    fprintf(stderr, "ERROR: %s at line %d\n", message, current.location.line);
    return current;
}

static char *parser_get_lexeme(Parser *parser, Token token) {
    char *lexeme = ocl_malloc(token.lexeme_length + 1);
    if (token.lexeme) {
        strcpy(lexeme, token.lexeme);
    }
    return lexeme;
}

/* Skip all newline tokens */
static void parser_skip_newlines(Parser *parser) {
    while (parser_match(parser, TOKEN_NEWLINE)) {
        /* Consume newlines */
    }
}

/* Forward declarations */
static ASTNode *parser_parse_statement(Parser *parser);
static ExprNode *parser_parse_expression(Parser *parser);
static ExprNode *parser_parse_assignment(Parser *parser);
static ExprNode *parser_parse_logic_or(Parser *parser);
static ExprNode *parser_parse_logic_and(Parser *parser);
static ExprNode *parser_parse_equality(Parser *parser);
static ExprNode *parser_parse_comparison(Parser *parser);
static ExprNode *parser_parse_addition(Parser *parser);
static ExprNode *parser_parse_multiplication(Parser *parser);
static ExprNode *parser_parse_unary(Parser *parser);
static ExprNode *parser_parse_primary(Parser *parser);
static ExprNode *parser_parse_call(Parser *parser);
static TypeNode *parser_parse_type(Parser *parser);
static BlockNode *parser_parse_block(Parser *parser);

/* Type parsing */
static TypeNode *parser_parse_type(Parser *parser) {
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        fprintf(stderr, "ERROR: Expected type name\n");
        return NULL;
    }
    
    Token type_token = parser_current(parser);
    parser_advance(parser);
    
    char *type_name = parser_get_lexeme(parser, type_token);
    
    BuiltinType type = TYPE_UNKNOWN;
    int bit_width = 0;
    
    /* Map type names to built-in types */
    if (strcmp(type_name, "Int") == 0 || strcmp(type_name, "int") == 0) {
        type = TYPE_INT;
        /* Check for bit width */
        if (parser_match(parser, TOKEN_INT)) {
            int val = parser_previous(parser).value.int_value;
            if (val == 32 || val == 64) {
                bit_width = val;
            }
        }
    } else if (strcmp(type_name, "Float") == 0 || strcmp(type_name, "float") == 0) {
        type = TYPE_FLOAT;
    } else if (strcmp(type_name, "String") == 0 || strcmp(type_name, "string") == 0) {
        type = TYPE_STRING;
    } else if (strcmp(type_name, "Bool") == 0 || strcmp(type_name, "bool") == 0) {
        type = TYPE_BOOL;
    } else if (strcmp(type_name, "Char") == 0 || strcmp(type_name, "char") == 0) {
        type = TYPE_CHAR;
    }
    
    ocl_free(type_name);
    
    TypeNode *type_node = ast_create_type(type, bit_width);
    
    /* Check for array syntax */
    if (parser_match(parser, TOKEN_LBRACKET)) {
        type_node->is_array = true;
        parser_consume(parser, TOKEN_RBRACKET, "Expected ']'");
    }
    
    return type_node;
}

/* Expression parsing with operator precedence */
static ExprNode *parser_parse_expression(Parser *parser) {
    return parser_parse_assignment(parser);
}

static ExprNode *parser_parse_assignment(Parser *parser) {
    ExprNode *expr = parser_parse_logic_or(parser);
    
    if (parser_match(parser, TOKEN_EQUAL)) {
        ExprNode *value = parser_parse_assignment(parser);
        if (expr->base.type == AST_IDENTIFIER) {
            IdentifierNode *id = (IdentifierNode *)expr;
            return ast_create_binary_op(expr->base.location, expr, "=", value);
        }
    }
    
    return expr;
}

static ExprNode *parser_parse_logic_or(Parser *parser) {
    ExprNode *expr = parser_parse_logic_and(parser);
    
    while (parser_match(parser, TOKEN_PIPE_PIPE)) {
        Token op = parser_previous(parser);
        ExprNode *right = parser_parse_logic_and(parser);
        expr = ast_create_binary_op(op.location, expr, "||", right);
    }
    
    return expr;
}

static ExprNode *parser_parse_logic_and(Parser *parser) {
    ExprNode *expr = parser_parse_equality(parser);
    
    while (parser_match(parser, TOKEN_AMPERSAND_AMPERSAND)) {
        Token op = parser_previous(parser);
        ExprNode *right = parser_parse_equality(parser);
        expr = ast_create_binary_op(op.location, expr, "&&", right);
    }
    
    return expr;
}

static ExprNode *parser_parse_equality(Parser *parser) {
    ExprNode *expr = parser_parse_comparison(parser);
    
    while (true) {
        Token op = parser_current(parser);
        if (parser_match(parser, TOKEN_EQUAL_EQUAL)) {
            ExprNode *right = parser_parse_comparison(parser);
            expr = ast_create_binary_op(op.location, expr, "==", right);
        } else if (parser_match(parser, TOKEN_BANG_EQUAL)) {
            ExprNode *right = parser_parse_comparison(parser);
            expr = ast_create_binary_op(op.location, expr, "!=", right);
        } else {
            break;
        }
    }
    
    return expr;
}

static ExprNode *parser_parse_comparison(Parser *parser) {
    ExprNode *expr = parser_parse_addition(parser);
    
    while (true) {
        Token op = parser_current(parser);
        if (parser_match(parser, TOKEN_LESS)) {
            ExprNode *right = parser_parse_addition(parser);
            expr = ast_create_binary_op(op.location, expr, "<", right);
        } else if (parser_match(parser, TOKEN_LESS_EQUAL)) {
            ExprNode *right = parser_parse_addition(parser);
            expr = ast_create_binary_op(op.location, expr, "<=", right);
        } else if (parser_match(parser, TOKEN_GREATER)) {
            ExprNode *right = parser_parse_addition(parser);
            expr = ast_create_binary_op(op.location, expr, ">", right);
        } else if (parser_match(parser, TOKEN_GREATER_EQUAL)) {
            ExprNode *right = parser_parse_addition(parser);
            expr = ast_create_binary_op(op.location, expr, ">=", right);
        } else {
            break;
        }
    }
    
    return expr;
}

static ExprNode *parser_parse_addition(Parser *parser) {
    ExprNode *expr = parser_parse_multiplication(parser);
    
    while (true) {
        Token op = parser_current(parser);
        if (parser_match(parser, TOKEN_PLUS)) {
            ExprNode *right = parser_parse_multiplication(parser);
            expr = ast_create_binary_op(op.location, expr, "+", right);
        } else if (parser_match(parser, TOKEN_MINUS)) {
            ExprNode *right = parser_parse_multiplication(parser);
            expr = ast_create_binary_op(op.location, expr, "-", right);
        } else {
            break;
        }
    }
    
    return expr;
}

static ExprNode *parser_parse_multiplication(Parser *parser) {
    ExprNode *expr = parser_parse_unary(parser);
    
    while (true) {
        Token op = parser_current(parser);
        if (parser_match(parser, TOKEN_STAR)) {
            ExprNode *right = parser_parse_unary(parser);
            expr = ast_create_binary_op(op.location, expr, "*", right);
        } else if (parser_match(parser, TOKEN_SLASH)) {
            ExprNode *right = parser_parse_unary(parser);
            expr = ast_create_binary_op(op.location, expr, "/", right);
        } else if (parser_match(parser, TOKEN_PERCENT)) {
            ExprNode *right = parser_parse_unary(parser);
            expr = ast_create_binary_op(op.location, expr, "%", right);
        } else {
            break;
        }
    }
    
    return expr;
}

static ExprNode *parser_parse_unary(Parser *parser) {
    Token op = parser_current(parser);
    
    if (parser_match(parser, TOKEN_BANG)) {
        ExprNode *expr = parser_parse_unary(parser);
        UnaryOpNode *node = ocl_malloc(sizeof(UnaryOpNode));
        node->base.type = AST_UNARY_OP;
        node->base.location = op.location;
        node->operand = expr;
        node->operator = "!";
        return (ExprNode *)node;
    } else if (parser_match(parser, TOKEN_MINUS)) {
        ExprNode *expr = parser_parse_unary(parser);
        UnaryOpNode *node = ocl_malloc(sizeof(UnaryOpNode));
        node->base.type = AST_UNARY_OP;
        node->base.location = op.location;
        node->operand = expr;
        node->operator = "-";
        return (ExprNode *)node;
    }
    
    return parser_parse_call(parser);
}

static ExprNode *parser_parse_call(Parser *parser) {
    ExprNode *expr = parser_parse_primary(parser);
    
    /* Handle function calls */
    if (parser_check(parser, TOKEN_LPAREN) && expr->base.type == AST_IDENTIFIER) {
        IdentifierNode *id = (IdentifierNode *)expr;
        parser_advance(parser);  /* consume ( */
        
        ExprNode **args = NULL;
        size_t arg_count = 0;
        
        if (!parser_check(parser, TOKEN_RPAREN)) {
            do {
                args = ocl_realloc(args, (arg_count + 1) * sizeof(ExprNode *));
                args[arg_count++] = parser_parse_assignment(parser);
                
                /* Special handling for printf-style syntax: format_string : arg1, arg2 */
                if ((strcmp(id->name, "printf") == 0 || strcmp(id->name, "print") == 0) && 
                    arg_count == 1 && parser_match(parser, TOKEN_COLON)) {
                    /* After the colon, parse remaining arguments */
                    if (!parser_check(parser, TOKEN_RPAREN)) {
                        do {
                            args = ocl_realloc(args, (arg_count + 1) * sizeof(ExprNode *));
                            args[arg_count++] = parser_parse_assignment(parser);
                        } while (parser_match(parser, TOKEN_COMMA));
                    }
                    break;  /* Don't try to find another comma */
                }
            } while (parser_match(parser, TOKEN_COMMA));
        }
        
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'");
        
        return ast_create_call(expr->base.location, id->name, args, arg_count);
    }
    
    /* Handle array indexing */
    if (parser_match(parser, TOKEN_LBRACKET)) {
        ExprNode *index = parser_parse_expression(parser);
        parser_consume(parser, TOKEN_RBRACKET, "Expected ']'");
        
        ExprNode *node = ocl_malloc(sizeof(ExprNode));
        node->base.type = AST_INDEX_ACCESS;
        node->base.location = expr->base.location;
        node->index_access.array = expr;
        node->index_access.index = index;
        return node;
    }
    
    return expr;
}

static ExprNode *parser_parse_primary(Parser *parser) {
    Token token = parser_current(parser);
    
    if (parser_match(parser, TOKEN_TRUE)) {
        return ast_create_literal(token.location, value_bool(true));
    }
    if (parser_match(parser, TOKEN_FALSE)) {
        return ast_create_literal(token.location, value_bool(false));
    }
    if (parser_match(parser, TOKEN_INT)) {
        return ast_create_literal(token.location, value_int(token.value.int_value));
    }
    if (parser_match(parser, TOKEN_FLOAT)) {
        return ast_create_literal(token.location, value_float(token.value.float_value));
    }
    if (parser_match(parser, TOKEN_STRING)) {
        char *str = ocl_malloc(token.value.string_length + 1);
        strcpy(str, token.value.string_value);
        return ast_create_literal(token.location, value_string(str));
    }
    if (parser_match(parser, TOKEN_CHAR)) {
        if (token.value.string_length > 0) {
            return ast_create_literal(token.location, value_char(token.value.string_value[0]));
        }
    }
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        char *name = parser_get_lexeme(parser, token);
        return ast_create_identifier(token.location, name);
    }
    if (parser_match(parser, TOKEN_LPAREN)) {
        ExprNode *expr = parser_parse_expression(parser);
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'");
        return expr;
    }
    
    fprintf(stderr, "ERROR: Unexpected token in expression: %s\n", token.lexeme);
    return NULL;
}

/* Statement parsing */
static BlockNode *parser_parse_block(Parser *parser) {
    parser_consume(parser, TOKEN_LBRACE, "Expected '{'");
    parser_skip_newlines(parser);
    
    SourceLocation loc = parser_previous(parser).location;
    BlockNode *block = ast_create_block(loc);
    
    while (!parser_check(parser, TOKEN_RBRACE) && !parser_is_at_end(parser)) {
        parser_skip_newlines(parser);
        ASTNode *stmt = parser_parse_statement(parser);
        if (stmt) {
            ast_add_statement(block, stmt);
        }
    }
    
    parser_skip_newlines(parser);
    parser_consume(parser, TOKEN_RBRACE, "Expected '}'");
    return block;
}

static ASTNode *parser_parse_statement(Parser *parser) {
    /* Import statement */
    if (parser_match(parser, TOKEN_IMPORT)) {
        SourceLocation loc = parser_previous(parser).location;
        
        parser_consume(parser, TOKEN_LESS, "Expected '<'");
        
        if (!parser_check(parser, TOKEN_IDENTIFIER)) {
            fprintf(stderr, "ERROR: Expected filename in import statement\n");
            return NULL;
        }
        
        Token filename_token = parser_current(parser);
        char *filename = parser_get_lexeme(parser, filename_token);
        parser_advance(parser);
        
        /* Also accept .sxh extension as identifier */
        if (parser_match(parser, TOKEN_DOT)) {
            if (parser_check(parser, TOKEN_IDENTIFIER)) {
                char *ext = parser_get_lexeme(parser, parser_current(parser));
                parser_advance(parser);
                char *full_filename = ocl_malloc(strlen(filename) + strlen(ext) + 2);
                sprintf(full_filename, "%s.%s", filename, ext);
                ocl_free(filename);
                filename = full_filename;
            }
        }
        
        parser_consume(parser, TOKEN_GREATER, "Expected '>'");
        parser_match(parser, TOKEN_SEMICOLON);  /* Optional semicolon */
        
        ImportNode *node = ocl_malloc(sizeof(ImportNode));
        node->base.type = AST_IMPORT;
        node->base.location = loc;
        node->filename = filename;
        
        return (ASTNode *)node;
    }
    
    /* Variable declaration with Let: Let x:Type = value */
    if (parser_match(parser, TOKEN_LET)) {
        SourceLocation loc = parser_previous(parser).location;
        
        if (!parser_check(parser, TOKEN_IDENTIFIER)) {
            fprintf(stderr, "ERROR: Expected variable name\n");
            return NULL;
        }
        
        Token name_token = parser_current(parser);
        char *name = parser_get_lexeme(parser, name_token);
        parser_advance(parser);
        
        parser_consume(parser, TOKEN_COLON, "Expected ':' in variable declaration");
        
        TypeNode *type = parser_parse_type(parser);
        
        ExprNode *initializer = NULL;
        if (parser_match(parser, TOKEN_EQUAL)) {
            initializer = parser_parse_expression(parser);
        }
        
        parser_match(parser, TOKEN_SEMICOLON);  /* Optional semicolon */
        
        return ast_create_var_decl(loc, name, type, initializer);
    }
    
    /* C-style variable declaration: Type x = value or Type x; */
    if (parser_check(parser, TOKEN_IDENTIFIER)) {
        /* Peek ahead to check if this is a type declaration */
        size_t saved_current = parser->current;
        Token potential_type = parser_current(parser);
        char *potential_type_name = parser_get_lexeme(parser, potential_type);
        
        bool is_builtin_type = strcmp(potential_type_name, "int") == 0 || 
                               strcmp(potential_type_name, "Int") == 0 ||
                               strcmp(potential_type_name, "float") == 0 ||
                               strcmp(potential_type_name, "Float") == 0 ||
                               strcmp(potential_type_name, "string") == 0 ||
                               strcmp(potential_type_name, "String") == 0 ||
                               strcmp(potential_type_name, "bool") == 0 ||
                               strcmp(potential_type_name, "Bool") == 0 ||
                               strcmp(potential_type_name, "char") == 0 ||
                               strcmp(potential_type_name, "Char") == 0;
        
        ocl_free(potential_type_name);
        
        if (is_builtin_type) {
            /* This looks like a type, parse the whole declaration */
            parser->current = saved_current;
            SourceLocation loc = parser_current(parser).location;
            
            TypeNode *type = parser_parse_type(parser);
            
            if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                fprintf(stderr, "ERROR: Expected variable name after type\n");
                parser->current = saved_current;
                /* Fall through to parse as expression */
            } else {
                Token name_token = parser_current(parser);
                char *name = parser_get_lexeme(parser, name_token);
                parser_advance(parser);
                
                ExprNode *initializer = NULL;
                if (parser_match(parser, TOKEN_EQUAL)) {
                    initializer = parser_parse_expression(parser);
                }
                
                parser_match(parser, TOKEN_SEMICOLON);  /* Optional semicolon */
                
                return ast_create_var_decl(loc, name, type, initializer);
            }
        } else {
            parser->current = saved_current;
        }
    }
    
    
    /* If statement */
    if (parser_match(parser, TOKEN_IF)) {
        SourceLocation loc = parser_previous(parser).location;
        
        parser_consume(parser, TOKEN_LPAREN, "Expected '('");
        ExprNode *condition = parser_parse_expression(parser);
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'");
        
        BlockNode *then_block = parser_parse_block(parser);
        BlockNode *else_block = NULL;
        
        if (parser_match(parser, TOKEN_ELSE)) {
            else_block = parser_parse_block(parser);
        }
        
        return ast_create_if_stmt(loc, condition, then_block, else_block);
    }
    
    /* While loop */
    if (parser_match(parser, TOKEN_WHILE)) {
        SourceLocation loc = parser_previous(parser).location;
        
        parser_consume(parser, TOKEN_LPAREN, "Expected '('");
        ExprNode *condition = parser_parse_expression(parser);
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'");
        
        BlockNode *body = parser_parse_block(parser);
        
        LoopNode *loop = ocl_malloc(sizeof(LoopNode));
        loop->base.type = AST_WHILE_LOOP;
        loop->base.location = loc;
        loop->is_for = false;
        loop->condition = condition;
        loop->body = body;
        loop->init = NULL;
        loop->increment = NULL;
        
        return (ASTNode *)loop;
    }
    
    /* For loop */
    if (parser_match(parser, TOKEN_FOR)) {
        SourceLocation loc = parser_previous(parser).location;
        
        parser_consume(parser, TOKEN_LPAREN, "Expected '('");
        
        /* Parse init */
        ASTNode *init = NULL;
        if (parser_match(parser, TOKEN_LET)) {
            /* Variable declaration */
            Token name_token = parser_current(parser);
            char *name = parser_get_lexeme(parser, name_token);
            parser_advance(parser);
            
            parser_consume(parser, TOKEN_COLON, "Expected ':'");
            TypeNode *type = parser_parse_type(parser);
            
            ExprNode *initializer = NULL;
            if (parser_match(parser, TOKEN_EQUAL)) {
                initializer = parser_parse_expression(parser);
            }
            
            init = ast_create_var_decl(loc, name, type, initializer);
        }
        parser_match(parser, TOKEN_SEMICOLON);
        
        /* Parse condition */
        ExprNode *condition = NULL;
        if (!parser_check(parser, TOKEN_SEMICOLON)) {
            condition = parser_parse_expression(parser);
        }
        parser_match(parser, TOKEN_SEMICOLON);
        
        /* Parse increment */
        ASTNode *increment = NULL;
        if (!parser_check(parser, TOKEN_RPAREN)) {
            ExprNode *inc_expr = parser_parse_expression(parser);
            // Convert expression to statement
            increment = (ASTNode *)inc_expr;
        }
        
        parser_consume(parser, TOKEN_RPAREN, "Expected ')'");
        
        BlockNode *body = parser_parse_block(parser);
        
        LoopNode *loop = ocl_malloc(sizeof(LoopNode));
        loop->base.type = AST_FOR_LOOP;
        loop->base.location = loc;
        loop->is_for = true;
        loop->init = init;
        loop->condition = condition;
        loop->increment = increment;
        loop->body = body;
        
        return (ASTNode *)loop;
    }
    
    /* Return statement */
    if (parser_match(parser, TOKEN_RETURN)) {
        SourceLocation loc = parser_previous(parser).location;
        
        ExprNode *value = NULL;
        if (!parser_check(parser, TOKEN_SEMICOLON) && !parser_check(parser, TOKEN_RBRACE)) {
            value = parser_parse_expression(parser);
        }
        
        parser_match(parser, TOKEN_SEMICOLON);
        
        return ast_create_return(loc, value);
    }
    
    /* Expression statement (e.g., function calls, assignments) */
    if (parser_check(parser, TOKEN_IDENTIFIER) || parser_check(parser, TOKEN_INT) || 
        parser_check(parser, TOKEN_FLOAT) || parser_check(parser, TOKEN_STRING) ||
        parser_check(parser, TOKEN_TRUE) || parser_check(parser, TOKEN_FALSE) ||
        parser_check(parser, TOKEN_LPAREN)) {
        SourceLocation loc = parser_current(parser).location;
        ExprNode *expr = parser_parse_expression(parser);
        parser_match(parser, TOKEN_SEMICOLON);
        
        /* Create a wrapper for expression statements */
        if (expr) {
            /* Return the expression directly - codegen will handle it */
            return (ASTNode *)expr;
        }
    }
    
    /* Expression statement */
    ExprNode *expr = parser_parse_expression(parser);
    if (!expr) {
        fprintf(stderr, "ERROR: Expected expression\n");
        return NULL;
    }
    
    parser_match(parser, TOKEN_SEMICOLON);  /* Optional semicolon */
    
    // Wrap expression in a statement node
    ASTNode *stmt = ocl_malloc(sizeof(ASTNode));
    stmt->type = AST_EXPR_STMT;
    stmt->location = expr->base.location;
    
    return stmt;
}

Parser *parser_create(Token *tokens, size_t token_count, const char *filename) {
    Parser *parser = ocl_malloc(sizeof(Parser));
    parser->tokens = tokens;
    parser->token_count = token_count;
    parser->current = 0;
    parser->filename = filename;
    return parser;
}

void parser_free(Parser *parser) {
    if (parser) {
        ocl_free(parser);
    }
}

ProgramNode *parser_parse(Parser *parser) {
    ProgramNode *program = ocl_malloc(sizeof(ProgramNode));
    program->base.type = AST_PROGRAM;
    program->base.location = (SourceLocation){1, 1, parser->filename};
    program->nodes = NULL;
    program->node_count = 0;
    
    while (!parser_is_at_end(parser)) {
        /* Skip newlines */
        while (parser_match(parser, TOKEN_NEWLINE)) {}
        
        if (parser_is_at_end(parser)) break;
        
        /* Handle imports and variable declarations at top level */
        if (parser_check(parser, TOKEN_IMPORT) || parser_check(parser, TOKEN_LET) || 
            parser_check(parser, TOKEN_RETURN)) {
            ASTNode *stmt = parser_parse_statement(parser);
            if (stmt) {
                program->nodes = ocl_realloc(program->nodes, (program->node_count + 1) * sizeof(ASTNode *));
                program->nodes[program->node_count++] = stmt;
            }
        }
        /* Function declaration */
        else if (parser_check(parser, TOKEN_FUNC)) {
            parser_advance(parser);
            SourceLocation loc = parser_previous(parser).location;
            
            /* Parse optional return type */
            TypeNode *return_type = NULL;
            if (parser_check(parser, TOKEN_IDENTIFIER)) {
                /* Peek ahead to see if this identifier is a return type or function name */
                size_t saved_current = parser->current;
                Token potential_type = parser_current(parser);
                char *potential_type_name = parser_get_lexeme(parser, potential_type);
                
                /* Check if it's a known type */
                bool is_known_type = strcmp(potential_type_name, "int") == 0 || 
                                     strcmp(potential_type_name, "Int") == 0 ||
                                     strcmp(potential_type_name, "float") == 0 ||
                                     strcmp(potential_type_name, "Float") == 0 ||
                                     strcmp(potential_type_name, "string") == 0 ||
                                     strcmp(potential_type_name, "String") == 0 ||
                                     strcmp(potential_type_name, "bool") == 0 ||
                                     strcmp(potential_type_name, "Bool") == 0 ||
                                     strcmp(potential_type_name, "char") == 0 ||
                                     strcmp(potential_type_name, "Char") == 0;
                
                ocl_free(potential_type_name);
                
                if (is_known_type) {
                    /* This identifier is a return type, parse it */
                    return_type = parser_parse_type(parser);
                } else {
                    /* This identifier is the function name, no explicit return type */
                    return_type = ast_create_type(TYPE_VOID, 0);
                    parser->current = saved_current;  /* Restore position */
                }
            } else {
                return_type = ast_create_type(TYPE_VOID, 0);
            }
            
            if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                fprintf(stderr, "ERROR: Expected function name\n");
                break;
            }
            
            Token name_token = parser_current(parser);
            char *name = parser_get_lexeme(parser, name_token);
            parser_advance(parser);
            
            parser_consume(parser, TOKEN_LPAREN, "Expected '('");
            
            ParamNode **params = NULL;
            size_t param_count = 0;
            
            if (!parser_check(parser, TOKEN_RPAREN)) {
                do {
                    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                        fprintf(stderr, "ERROR: Expected parameter name\n");
                        break;
                    }
                    
                    Token param_name = parser_current(parser);
                    char *param_name_str = parser_get_lexeme(parser, param_name);
                    parser_advance(parser);
                    
                    parser_consume(parser, TOKEN_COLON, "Expected ':'");
                    TypeNode *param_type = parser_parse_type(parser);
                    
                    params = ocl_realloc(params, (param_count + 1) * sizeof(ParamNode *));
                    params[param_count++] = ast_create_param(param_name_str, param_type, param_name.location);
                } while (parser_match(parser, TOKEN_COMMA));
            }
            
            parser_consume(parser, TOKEN_RPAREN, "Expected ')'");
            
            BlockNode *body = parser_parse_block(parser);
            
            ASTNode *func_decl = ast_create_func_decl(loc, name, return_type, params, param_count, body);
            
            program->nodes = ocl_realloc(program->nodes, (program->node_count + 1) * sizeof(ASTNode *));
            program->nodes[program->node_count++] = func_decl;
        } else {
            ASTNode *stmt = parser_parse_statement(parser);
            if (stmt) {
                program->nodes = ocl_realloc(program->nodes, (program->node_count + 1) * sizeof(ASTNode *));
                program->nodes[program->node_count++] = stmt;
            }
        }
    }
    
    return program;
}
