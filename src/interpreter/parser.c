#include "parser.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Helpers
═══════════════════════════════════════════════════════════════ */

static bool at_end(Parser *p) {
    /* skip newlines when checking */
    while (p->current < p->token_count &&
           p->tokens[p->current].type == TOKEN_NEWLINE)
        p->current++;
    return p->current >= p->token_count ||
           p->tokens[p->current].type == TOKEN_EOF;
}

static Token *cur_tok(Parser *p) {
    /* skip newlines transparently */
    while (p->current < p->token_count &&
           p->tokens[p->current].type == TOKEN_NEWLINE)
        p->current++;
    return &p->tokens[p->current < p->token_count ? p->current : p->token_count - 1];
}

static Token *prev_tok(Parser *p) {
    /* walk back over newlines */
    size_t i = p->current;
    while (i > 0 && p->tokens[i - 1].type == TOKEN_NEWLINE) i--;
    return &p->tokens[i > 0 ? i - 1 : 0];
}

static bool check(Parser *p, TokenType t) {
    return cur_tok(p)->type == t;
}

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { p->current++; return true; }
    return false;
}

static Token consume(Parser *p, TokenType t, const char *msg) {
    if (check(p, t)) {
        Token tok = *cur_tok(p);
        p->current++;
        return tok;
    }
    Token *c = cur_tok(p);
    SourceLocation loc = c->location;
    if (p->errors)
        error_add(p->errors, ERROR_PARSER, loc,
                  "%s (got '%s')", msg, c->lexeme ? c->lexeme : "?");
    else
        fprintf(stderr, "PARSE ERROR [%d:%d]: %s\n", loc.line, loc.column, msg);
    return *c;
}

/* Duplicate the lexeme of a token into a new heap string */
static char *dup_lexeme(const Token *t) {
    if (!t->lexeme) return ocl_strdup("");
    return ocl_strdup(t->lexeme);
}

/* ═══════════════════════════════════════════════════════════════
   Forward declarations
═══════════════════════════════════════════════════════════════ */
static ASTNode  *parse_statement(Parser *p);
static ExprNode *parse_expression(Parser *p);
static ExprNode *parse_assignment(Parser *p);
static ExprNode *parse_or(Parser *p);
static ExprNode *parse_and(Parser *p);
static ExprNode *parse_equality(Parser *p);
static ExprNode *parse_comparison(Parser *p);
static ExprNode *parse_addition(Parser *p);
static ExprNode *parse_multiplication(Parser *p);
static ExprNode *parse_unary(Parser *p);
static ExprNode *parse_call_expr(Parser *p);
static ExprNode *parse_primary(Parser *p);
static BlockNode *parse_block(Parser *p);
static TypeNode  *parse_type(Parser *p);

/* ═══════════════════════════════════════════════════════════════
   Type parsing
═══════════════════════════════════════════════════════════════ */
static bool is_type_name(const char *s) {
    return !strcmp(s,"int")    || !strcmp(s,"Int")   ||
           !strcmp(s,"float")  || !strcmp(s,"Float") ||
           !strcmp(s,"string") || !strcmp(s,"String")||
           !strcmp(s,"bool")   || !strcmp(s,"Bool")  ||
           !strcmp(s,"char")   || !strcmp(s,"Char")  ||
           !strcmp(s,"void")   || !strcmp(s,"Void");
}

static TypeNode *parse_type(Parser *p) {
    Token *t = cur_tok(p);
    if (t->type != TOKEN_IDENTIFIER) {
        error_add(p->errors, ERROR_PARSER, t->location, "Expected type name");
        return ast_create_type(TYPE_UNKNOWN, 0);
    }
    char *name = dup_lexeme(t);
    p->current++;

    BuiltinType bt = TYPE_UNKNOWN;
    if      (!strcmp(name,"int")    || !strcmp(name,"Int"))    bt = TYPE_INT;
    else if (!strcmp(name,"float")  || !strcmp(name,"Float"))  bt = TYPE_FLOAT;
    else if (!strcmp(name,"string") || !strcmp(name,"String")) bt = TYPE_STRING;
    else if (!strcmp(name,"bool")   || !strcmp(name,"Bool"))   bt = TYPE_BOOL;
    else if (!strcmp(name,"char")   || !strcmp(name,"Char"))   bt = TYPE_CHAR;
    else if (!strcmp(name,"void")   || !strcmp(name,"Void"))   bt = TYPE_VOID;
    ocl_free(name);

    int bw = 0;
    /* optional bit-width suffix: int32, int64 */
    if (bt == TYPE_INT && check(p, TOKEN_INT)) {
        int64_t v = cur_tok(p)->value.int_value;
        if (v == 32 || v == 64) { bw = (int)v; p->current++; }
    }

    TypeNode *tn = ast_create_type(bt, bw);

    /* optional array brackets */
    if (match(p, TOKEN_LBRACKET)) {
        tn->is_array = true;
        consume(p, TOKEN_RBRACKET, "Expected ']' after '['");
    }
    return tn;
}

/* ═══════════════════════════════════════════════════════════════
   Expression parsing  (Pratt-style precedence climb)
═══════════════════════════════════════════════════════════════ */

static ExprNode *parse_expression(Parser *p) { return parse_assignment(p); }

static ExprNode *parse_assignment(Parser *p) {
    ExprNode *lhs = parse_or(p);
    if (!lhs) return NULL;

    if (match(p, TOKEN_EQUAL)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *rhs = parse_assignment(p);
        return ast_create_binary_op(loc, lhs, "=", rhs);
    }
    return lhs;
}

static ExprNode *parse_or(Parser *p) {
    ExprNode *e = parse_and(p);
    while (match(p, TOKEN_PIPE_PIPE)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *r = parse_and(p);
        e = ast_create_binary_op(loc, e, "||", r);
    }
    return e;
}

static ExprNode *parse_and(Parser *p) {
    ExprNode *e = parse_equality(p);
    while (match(p, TOKEN_AMPERSAND_AMPERSAND)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *r = parse_equality(p);
        e = ast_create_binary_op(loc, e, "&&", r);
    }
    return e;
}

static ExprNode *parse_equality(Parser *p) {
    ExprNode *e = parse_comparison(p);
    for (;;) {
        if (match(p, TOKEN_EQUAL_EQUAL)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "==", parse_comparison(p));
        } else if (match(p, TOKEN_BANG_EQUAL)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "!=", parse_comparison(p));
        } else break;
    }
    return e;
}

static ExprNode *parse_comparison(Parser *p) {
    ExprNode *e = parse_addition(p);
    for (;;) {
        if (match(p, TOKEN_LESS)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "<", parse_addition(p));
        } else if (match(p, TOKEN_LESS_EQUAL)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "<=", parse_addition(p));
        } else if (match(p, TOKEN_GREATER)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, ">", parse_addition(p));
        } else if (match(p, TOKEN_GREATER_EQUAL)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, ">=", parse_addition(p));
        } else break;
    }
    return e;
}

static ExprNode *parse_addition(Parser *p) {
    ExprNode *e = parse_multiplication(p);
    for (;;) {
        if (match(p, TOKEN_PLUS)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "+", parse_multiplication(p));
        } else if (match(p, TOKEN_MINUS)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "-", parse_multiplication(p));
        } else break;
    }
    return e;
}

static ExprNode *parse_multiplication(Parser *p) {
    ExprNode *e = parse_unary(p);
    for (;;) {
        if (match(p, TOKEN_STAR)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "*", parse_unary(p));
        } else if (match(p, TOKEN_SLASH)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "/", parse_unary(p));
        } else if (match(p, TOKEN_PERCENT)) {
            SourceLocation loc = prev_tok(p)->location;
            e = ast_create_binary_op(loc, e, "%", parse_unary(p));
        } else break;
    }
    return e;
}

static ExprNode *parse_unary(Parser *p) {
    if (match(p, TOKEN_BANG)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u  = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type    = AST_UNARY_OP;
        u->base.location = loc;
        u->operator     = "!";
        u->operand      = parse_unary(p);
        return (ExprNode *)u;
    }
    if (match(p, TOKEN_MINUS)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u  = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type    = AST_UNARY_OP;
        u->base.location = loc;
        u->operator     = "-";
        u->operand      = parse_unary(p);
        return (ExprNode *)u;
    }
    return parse_call_expr(p);
}

/* Build argument list.  Handles printf/print special colon syntax. */
static ExprNode *parse_call_expr(Parser *p) {
    ExprNode *expr = parse_primary(p);
    if (!expr) return NULL;

    /* Function call */
    if (expr->base.type == AST_IDENTIFIER && check(p, TOKEN_LPAREN)) {
        IdentifierNode *id = (IdentifierNode *)expr;
        SourceLocation loc = expr->base.location;
        p->current++; /* consume ( */

        ExprNode **args   = NULL;
        size_t    arg_cnt = 0;
        bool      is_printf = (!strcmp(id->name,"printf") || !strcmp(id->name,"print"));

        if (!check(p, TOKEN_RPAREN)) {
            /* First argument */
            args = ocl_realloc(args, (arg_cnt + 1) * sizeof(ExprNode *));
            args[arg_cnt++] = parse_expression(p);

            /* printf/print:  "fmt" : arg1, arg2 */
            if (is_printf && match(p, TOKEN_COLON)) {
                while (!check(p, TOKEN_RPAREN) && !at_end(p)) {
                    args = ocl_realloc(args, (arg_cnt + 1) * sizeof(ExprNode *));
                    args[arg_cnt++] = parse_expression(p);
                    if (!match(p, TOKEN_COMMA)) break;
                }
            } else {
                while (match(p, TOKEN_COMMA)) {
                    args = ocl_realloc(args, (arg_cnt + 1) * sizeof(ExprNode *));
                    args[arg_cnt++] = parse_expression(p);
                }
            }
        }
        consume(p, TOKEN_RPAREN, "Expected ')' after arguments");

        char *fname = ocl_strdup(id->name);
        /* free the identifier node we already allocated */
        ocl_free(id->name);
        ocl_free(id);
        return ast_create_call(loc, fname, args, arg_cnt);
    }

    /* Array index */
    while (match(p, TOKEN_LBRACKET)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *index    = parse_expression(p);
        consume(p, TOKEN_RBRACKET, "Expected ']'");
        ExprNode *node = ocl_malloc(sizeof(ExprNode));
        node->base.type     = AST_INDEX_ACCESS;
        node->base.location = loc;
        node->index_access.array = expr;
        node->index_access.index = index;
        expr = node;
    }

    return expr;
}

static ExprNode *parse_primary(Parser *p) {
    Token *t = cur_tok(p);
    SourceLocation loc = t->location;

    if (match(p, TOKEN_TRUE))    return ast_create_literal(loc, value_bool(true));
    if (match(p, TOKEN_FALSE))   return ast_create_literal(loc, value_bool(false));

    if (check(p, TOKEN_INT)) {
        int64_t v = t->value.int_value;
        p->current++;
        return ast_create_literal(loc, value_int(v));
    }
    if (check(p, TOKEN_FLOAT)) {
        double v = t->value.float_value;
        p->current++;
        return ast_create_literal(loc, value_float(v));
    }
    if (check(p, TOKEN_STRING)) {
        char *s = ocl_strdup(t->value.string_value ? t->value.string_value : "");
        p->current++;
        return ast_create_literal(loc, value_string(s));
    }
    if (check(p, TOKEN_CHAR)) {
        char c = (t->value.string_value && t->value.string_length > 0)
                 ? t->value.string_value[0] : '\0';
        p->current++;
        return ast_create_literal(loc, value_char(c));
    }
    if (check(p, TOKEN_IDENTIFIER)) {
        char *name = dup_lexeme(t);
        p->current++;
        return ast_create_identifier(loc, name);
    }
    if (match(p, TOKEN_LPAREN)) {
        ExprNode *e = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')'");
        return e;
    }

    error_add(p->errors, ERROR_PARSER, loc,
              "Unexpected token '%s' in expression", t->lexeme ? t->lexeme : "?");
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
   Statement parsing
═══════════════════════════════════════════════════════════════ */

static BlockNode *parse_block(Parser *p) {
    SourceLocation loc = cur_tok(p)->location;
    consume(p, TOKEN_LBRACE, "Expected '{'");
    BlockNode *block = ast_create_block(loc);

    while (!check(p, TOKEN_RBRACE) && !at_end(p)) {
        ASTNode *stmt = parse_statement(p);
        if (stmt) ast_add_statement(block, stmt);
    }
    consume(p, TOKEN_RBRACE, "Expected '}'");
    return block;
}

/* Detect  TypeName varName  pattern */
static bool is_c_style_var_decl(Parser *p) {
    Token *t = cur_tok(p);
    if (t->type != TOKEN_IDENTIFIER) return false;
    if (!is_type_name(t->lexeme))    return false;
    /* next real token (skip newlines) must be another identifier */
    size_t saved = p->current;
    p->current++;
    Token *nxt = cur_tok(p);
    bool result = (nxt->type == TOKEN_IDENTIFIER);
    p->current = saved;
    return result;
}

static ASTNode *parse_statement(Parser *p) {
    Token *t = cur_tok(p);
    SourceLocation loc = t->location;

    /* ── Import ── */
    if (match(p, TOKEN_IMPORT)) {
        consume(p, TOKEN_LESS, "Expected '<' after Import");
        char name_buf[256] = {0};
        /* collect identifier + optional .ext */
        if (check(p, TOKEN_IDENTIFIER)) {
            strcat(name_buf, cur_tok(p)->lexeme);
            p->current++;
        }
        if (match(p, TOKEN_DOT) && check(p, TOKEN_IDENTIFIER)) {
            strcat(name_buf, ".");
            strcat(name_buf, cur_tok(p)->lexeme);
            p->current++;
        }
        consume(p, TOKEN_GREATER, "Expected '>'");
        match(p, TOKEN_SEMICOLON);

        ImportNode *n    = ocl_malloc(sizeof(ImportNode));
        n->base.type     = AST_IMPORT;
        n->base.location = loc;
        n->filename      = ocl_strdup(name_buf);
        return (ASTNode *)n;
    }

    /* ── Let declaration ── */
    if (match(p, TOKEN_LET)) {
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        char *name = dup_lexeme(&name_tok);
        consume(p, TOKEN_COLON, "Expected ':' after variable name");
        TypeNode *type = parse_type(p);
        ExprNode *init = NULL;
        if (match(p, TOKEN_EQUAL)) init = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return ast_create_var_decl(loc, name, type, init);
    }

    /* ── C-style var decl: int x = ...  or  int x; ── */
    if (is_c_style_var_decl(p)) {
        TypeNode *type   = parse_type(p);
        Token name_tok   = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        char *name       = dup_lexeme(&name_tok);
        ExprNode *init   = NULL;
        if (match(p, TOKEN_EQUAL)) init = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return ast_create_var_decl(loc, name, type, init);
    }

    /* ── If ── */
    if (match(p, TOKEN_IF)) {
        consume(p, TOKEN_LPAREN, "Expected '(' after if");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')' after if condition");
        BlockNode *then_b = parse_block(p);
        BlockNode *else_b = NULL;
        if (match(p, TOKEN_ELSE)) {
            if (check(p, TOKEN_IF)) {
                /* else if → wrap in a block */
                ASTNode *elif = parse_statement(p);
                else_b = ast_create_block(loc);
                ast_add_statement(else_b, elif);
            } else {
                else_b = parse_block(p);
            }
        }
        return ast_create_if_stmt(loc, cond, then_b, else_b);
    }

    /* ── While ── */
    if (match(p, TOKEN_WHILE)) {
        consume(p, TOKEN_LPAREN, "Expected '('");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')'");
        BlockNode *body = parse_block(p);
        LoopNode *lp    = ocl_malloc(sizeof(LoopNode));
        lp->base.type   = AST_WHILE_LOOP;
        lp->base.location = loc;
        lp->is_for      = false;
        lp->init        = NULL;
        lp->condition   = cond;
        lp->increment   = NULL;
        lp->body        = body;
        return (ASTNode *)lp;
    }

    /* ── For ── */
    if (match(p, TOKEN_FOR)) {
        consume(p, TOKEN_LPAREN, "Expected '('");

        ASTNode *init = NULL;
        /* init part */
        if (match(p, TOKEN_LET)) {
            Token n_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
            char *vname = dup_lexeme(&n_tok);
            consume(p, TOKEN_COLON, "Expected ':'");
            TypeNode *vtype = parse_type(p);
            ExprNode *vinit = NULL;
            if (match(p, TOKEN_EQUAL)) vinit = parse_expression(p);
            init = ast_create_var_decl(loc, vname, vtype, vinit);
        } else if (is_c_style_var_decl(p)) {
            TypeNode *vtype = parse_type(p);
            Token n_tok     = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
            char *vname     = dup_lexeme(&n_tok);
            ExprNode *vinit = NULL;
            if (match(p, TOKEN_EQUAL)) vinit = parse_expression(p);
            init = ast_create_var_decl(loc, vname, vtype, vinit);
        } else if (!check(p, TOKEN_SEMICOLON)) {
            init = (ASTNode *)parse_expression(p);
        }
        match(p, TOKEN_SEMICOLON);

        ExprNode *cond = NULL;
        if (!check(p, TOKEN_SEMICOLON)) cond = parse_expression(p);
        match(p, TOKEN_SEMICOLON);

        ASTNode *incr = NULL;
        if (!check(p, TOKEN_RPAREN)) incr = (ASTNode *)parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')'");

        BlockNode *body = parse_block(p);
        LoopNode *lp    = ocl_malloc(sizeof(LoopNode));
        lp->base.type   = AST_FOR_LOOP;
        lp->base.location = loc;
        lp->is_for      = true;
        lp->init        = init;
        lp->condition   = cond;
        lp->increment   = incr;
        lp->body        = body;
        return (ASTNode *)lp;
    }

    /* ── Return ── */
    if (match(p, TOKEN_RETURN)) {
        ExprNode *val = NULL;
        if (!check(p, TOKEN_SEMICOLON) && !check(p, TOKEN_RBRACE) && !at_end(p))
            val = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return ast_create_return(loc, val);
    }

    /* ── Break / Continue ── */
    if (match(p, TOKEN_BREAK)) {
        match(p, TOKEN_SEMICOLON);
        ASTNode *n = ocl_malloc(sizeof(ASTNode));
        n->type = AST_BREAK; n->location = loc;
        return n;
    }
    if (match(p, TOKEN_CONTINUE)) {
        match(p, TOKEN_SEMICOLON);
        ASTNode *n = ocl_malloc(sizeof(ASTNode));
        n->type = AST_CONTINUE; n->location = loc;
        return n;
    }

    /* ── Expression statement (assignments, calls, …) ── */
    ExprNode *expr = parse_expression(p);
    if (!expr) {
        /* skip bad token */
        p->current++;
        return NULL;
    }
    match(p, TOKEN_SEMICOLON);
    return (ASTNode *)expr;
}

/* ═══════════════════════════════════════════════════════════════
   Top-level: function declarations and global statements
═══════════════════════════════════════════════════════════════ */

Parser *parser_create(Token *tokens, size_t token_count,
                       const char *filename, ErrorCollector *errors) {
    Parser *p   = ocl_malloc(sizeof(Parser));
    p->tokens   = tokens;
    p->token_count = token_count;
    p->current  = 0;
    p->filename = filename;
    p->errors   = errors;
    return p;
}

void parser_free(Parser *p) { ocl_free(p); }

ProgramNode *parser_parse(Parser *p) {
    ProgramNode *prog  = ocl_malloc(sizeof(ProgramNode));
    prog->base.type    = AST_PROGRAM;
    prog->base.location = (SourceLocation){1, 1, p->filename};
    prog->nodes        = NULL;
    prog->node_count   = 0;

    while (!at_end(p)) {

        /* ── Function declaration ── */
        if (check(p, TOKEN_FUNC)) {
            p->current++; /* consume 'func' */
            SourceLocation loc = prev_tok(p)->location;

            /* Optional return type before function name */
            TypeNode *ret = ast_create_type(TYPE_VOID, 0);
            if (check(p, TOKEN_IDENTIFIER)) {
                char *maybe_type = cur_tok(p)->lexeme;
                if (is_type_name(maybe_type)) {
                    ocl_free(ret);
                    ret = parse_type(p);
                }
            }

            Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected function name");
            char *fname    = dup_lexeme(&name_tok);

            consume(p, TOKEN_LPAREN, "Expected '(' after function name");

            ParamNode **params = NULL;
            size_t      param_count = 0;

            if (!check(p, TOKEN_RPAREN)) {
                do {
                    Token pname_tok = consume(p, TOKEN_IDENTIFIER, "Expected parameter name");
                    char *pname     = dup_lexeme(&pname_tok);
                    consume(p, TOKEN_COLON, "Expected ':' after parameter name");
                    TypeNode *ptype = parse_type(p);
                    params = ocl_realloc(params, (param_count + 1) * sizeof(ParamNode *));
                    params[param_count++] = ast_create_param(pname, ptype, pname_tok.location);
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "Expected ')'");

            BlockNode *body = parse_block(p);

            ASTNode *fn = ast_create_func_decl(loc, fname, ret, params, param_count, body);
            prog->nodes = ocl_realloc(prog->nodes, (prog->node_count + 1) * sizeof(ASTNode *));
            prog->nodes[prog->node_count++] = fn;
        } else {
            /* global statement (Import, Let, …) */
            ASTNode *stmt = parse_statement(p);
            if (stmt) {
                prog->nodes = ocl_realloc(prog->nodes, (prog->node_count + 1) * sizeof(ASTNode *));
                prog->nodes[prog->node_count++] = stmt;
            }
        }
    }

    return prog;
}