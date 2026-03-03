#include "parser.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool at_end(Parser *p) {
    while (p->current < p->token_count && p->tokens[p->current].type == TOKEN_NEWLINE) p->current++;
    return p->current >= p->token_count || p->tokens[p->current].type == TOKEN_EOF;
}

static Token *cur_tok(Parser *p) {
    while (p->current < p->token_count && p->tokens[p->current].type == TOKEN_NEWLINE) p->current++;
    return &p->tokens[p->current < p->token_count ? p->current : p->token_count - 1];
}

static Token *prev_tok(Parser *p) {
    size_t i = p->current;
    while (i > 0 && p->tokens[i-1].type == TOKEN_NEWLINE) i--;
    return &p->tokens[i > 0 ? i-1 : 0];
}

static bool check(Parser *p, TokenType t) { return cur_tok(p)->type == t; }

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { p->current++; return true; }
    return false;
}

static Token consume(Parser *p, TokenType t, const char *msg) {
    if (check(p, t)) { Token tok = *cur_tok(p); p->current++; return tok; }
    Token *c = cur_tok(p); SourceLocation loc = c->location;
    if (p->errors) error_add(p->errors, ERROR_PARSER, loc, "%s (got '%s')", msg, c->lexeme ? c->lexeme : "?");
    else fprintf(stderr, "PARSE ERROR [%d:%d]: %s\n", loc.line, loc.column, msg);
    return *c;
}

static char *dup_lexeme(const Token *t) { return !t->lexeme ? ocl_strdup("") : ocl_strdup(t->lexeme); }

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

static bool is_type_name(const char *s) {
    return !strcmp(s,"int")||!strcmp(s,"Int")||!strcmp(s,"float")||!strcmp(s,"Float")||
           !strcmp(s,"string")||!strcmp(s,"String")||!strcmp(s,"bool")||!strcmp(s,"Bool")||
           !strcmp(s,"char")||!strcmp(s,"Char")||!strcmp(s,"void")||!strcmp(s,"Void");
}

static TypeNode *parse_type(Parser *p) {
    Token *t = cur_tok(p);
    if (t->type != TOKEN_IDENTIFIER) {
        error_add(p->errors, ERROR_PARSER, t->location, "Expected type name");
        return ast_create_type(TYPE_UNKNOWN, 0);
    }
    char *name = dup_lexeme(t); p->current++;
    BuiltinType bt = TYPE_UNKNOWN;
    if      (!strcmp(name,"int")||!strcmp(name,"Int"))    bt = TYPE_INT;
    else if (!strcmp(name,"float")||!strcmp(name,"Float")) bt = TYPE_FLOAT;
    else if (!strcmp(name,"string")||!strcmp(name,"String")) bt = TYPE_STRING;
    else if (!strcmp(name,"bool")||!strcmp(name,"Bool")) bt = TYPE_BOOL;
    else if (!strcmp(name,"char")||!strcmp(name,"Char")) bt = TYPE_CHAR;
    else if (!strcmp(name,"void")||!strcmp(name,"Void")) bt = TYPE_VOID;
    ocl_free(name);
    int bw = 0;
    if (bt == TYPE_INT && check(p, TOKEN_INT)) {
        int64_t v = cur_tok(p)->value.int_value;
        if (v == 32 || v == 64) { bw = (int)v; p->current++; }
    }
    TypeNode *tn = ast_create_type(bt, bw);
    if (match(p, TOKEN_LBRACKET)) { tn->is_array = true; consume(p, TOKEN_RBRACKET, "Expected ']'"); }
    return tn;
}

static ExprNode *parse_expression(Parser *p) { return parse_assignment(p); }

static ExprNode *parse_assignment(Parser *p) {
    ExprNode *lhs = parse_or(p); if (!lhs) return NULL;
    if (match(p, TOKEN_EQUAL)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *rhs = parse_assignment(p);
        return ast_create_binary_op(loc, lhs, "=", rhs);
    }
    /* Compound assignment: x op= y  →  x = x op y */
    static const struct { TokenType tok; const char *op; } compound[] = {
        { TOKEN_PLUS_EQUAL,    "+" },
        { TOKEN_MINUS_EQUAL,   "-" },
        { TOKEN_STAR_EQUAL,    "*" },
        { TOKEN_SLASH_EQUAL,   "/" },
        { TOKEN_PERCENT_EQUAL, "%" },
        { (TokenType)0, NULL }
    };
    for (int i = 0; compound[i].op; i++) {
        if (match(p, compound[i].tok)) {
            SourceLocation loc = prev_tok(p)->location;
            ExprNode *rhs = parse_assignment(p);
            /* Duplicate lhs as the left operand of the inner op */
            ExprNode *lhs_copy = NULL;
            if (lhs->base.type == AST_IDENTIFIER) {
                char *dup_name = ocl_strdup(((IdentifierNode *)lhs)->name);
                lhs_copy = ast_create_identifier(loc, dup_name);
            } else {
                /* For non-identifier lhs (e.g. array index), fall back to
                   a plain assignment — compound index ops aren't supported yet */
                lhs_copy = lhs;
            }
            ExprNode *arith = ast_create_binary_op(loc, lhs_copy, compound[i].op, rhs);
            return ast_create_binary_op(loc, lhs, "=", arith);
        }
    }
    return lhs;
}

static ExprNode *parse_or(Parser *p) {
    ExprNode *e = parse_and(p);
    while (match(p, TOKEN_PIPE_PIPE)) {
        SourceLocation loc = prev_tok(p)->location;
        e = ast_create_binary_op(loc, e, "||", parse_and(p));
    }
    return e;
}

static ExprNode *parse_and(Parser *p) {
    ExprNode *e = parse_equality(p);
    while (match(p, TOKEN_AMPERSAND_AMPERSAND)) {
        SourceLocation loc = prev_tok(p)->location;
        e = ast_create_binary_op(loc, e, "&&", parse_equality(p));
    }
    return e;
}

static ExprNode *parse_equality(Parser *p) {
    ExprNode *e = parse_comparison(p);
    for (;;) {
        if (match(p, TOKEN_EQUAL_EQUAL)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "==", parse_comparison(p)); }
        else if (match(p, TOKEN_BANG_EQUAL)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "!=", parse_comparison(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_comparison(Parser *p) {
    ExprNode *e = parse_addition(p);
    for (;;) {
        if (match(p, TOKEN_LESS)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "<", parse_addition(p)); }
        else if (match(p, TOKEN_LESS_EQUAL)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "<=", parse_addition(p)); }
        else if (match(p, TOKEN_GREATER)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, ">", parse_addition(p)); }
        else if (match(p, TOKEN_GREATER_EQUAL)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, ">=", parse_addition(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_addition(Parser *p) {
    ExprNode *e = parse_multiplication(p);
    for (;;) {
        if (match(p, TOKEN_PLUS)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "+", parse_multiplication(p)); }
        else if (match(p, TOKEN_MINUS)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "-", parse_multiplication(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_multiplication(Parser *p) {
    ExprNode *e = parse_unary(p);
    for (;;) {
        if (match(p, TOKEN_STAR)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "*", parse_unary(p)); }
        else if (match(p, TOKEN_SLASH)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "/", parse_unary(p)); }
        else if (match(p, TOKEN_PERCENT)) { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "%", parse_unary(p)); }
        else break;
    }
    return e;
}

static ExprNode *make_inc_dec(SourceLocation loc, ExprNode *var, const char *op) {
    ExprNode *one  = ast_create_literal(loc, value_int(1));
    char *dup_name = ocl_strdup(((IdentifierNode *)var)->name);
    ExprNode *var2 = ast_create_identifier(loc, dup_name);
    ExprNode *arith = ast_create_binary_op(loc, var2, op, one);
    return ast_create_binary_op(loc, var, "=", arith);
}

static ExprNode *parse_unary(Parser *p) {
    if (match(p, TOKEN_PLUS_PLUS)) {
        SourceLocation loc = prev_tok(p)->location;
        Token *t = cur_tok(p);
        if (t->type != TOKEN_IDENTIFIER) { error_add(p->errors, ERROR_PARSER, loc, "Expected identifier after '++'"); return NULL; }
        char *name = dup_lexeme(t); p->current++;
        return make_inc_dec(loc, ast_create_identifier(loc, name), "+");
    }
    if (match(p, TOKEN_MINUS_MINUS)) {
        SourceLocation loc = prev_tok(p)->location;
        Token *t = cur_tok(p);
        if (t->type != TOKEN_IDENTIFIER) { error_add(p->errors, ERROR_PARSER, loc, "Expected identifier after '--'"); return NULL; }
        char *name = dup_lexeme(t); p->current++;
        return make_inc_dec(loc, ast_create_identifier(loc, name), "-");
    }
    if (match(p, TOKEN_BANG)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type = AST_UNARY_OP; u->base.location = loc; u->operator = "!"; u->operand = parse_unary(p);
        return (ExprNode *)u;
    }
    if (match(p, TOKEN_MINUS)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type = AST_UNARY_OP; u->base.location = loc; u->operator = "-"; u->operand = parse_unary(p);
        return (ExprNode *)u;
    }
    return parse_call_expr(p);
}

static ExprNode *parse_call_expr(Parser *p) {
    ExprNode *expr = parse_primary(p);
    if (!expr) return NULL;

    /* Function call */
    if (expr->base.type == AST_IDENTIFIER && check(p, TOKEN_LPAREN)) {
        IdentifierNode *id = (IdentifierNode *)expr;
        SourceLocation loc = expr->base.location;
        p->current++;
        ExprNode **args = NULL; size_t arg_cnt = 0;
        bool is_printf = (!strcmp(id->name,"printf")||!strcmp(id->name,"print"));
        if (!check(p, TOKEN_RPAREN)) {
            args = ocl_realloc(args, (arg_cnt+1)*sizeof(ExprNode *));
            args[arg_cnt++] = parse_expression(p);
            if (is_printf && match(p, TOKEN_COLON)) {
                while (!check(p, TOKEN_RPAREN) && !at_end(p)) {
                    args = ocl_realloc(args, (arg_cnt+1)*sizeof(ExprNode *));
                    args[arg_cnt++] = parse_expression(p);
                    if (!match(p, TOKEN_COMMA)) break;
                }
            } else {
                while (match(p, TOKEN_COMMA)) {
                    args = ocl_realloc(args, (arg_cnt+1)*sizeof(ExprNode *));
                    args[arg_cnt++] = parse_expression(p);
                }
            }
        }
        consume(p, TOKEN_RPAREN, "Expected ')' after arguments");
        char *fname = ocl_strdup(id->name);
        ocl_free(id->name); ocl_free(id);
        return ast_create_call(loc, fname, args, arg_cnt);
    }

    /* Postfix ++ / -- */
    if (expr->base.type == AST_IDENTIFIER) {
        SourceLocation loc = expr->base.location;
        if (match(p, TOKEN_PLUS_PLUS))  return make_inc_dec(loc, expr, "+");
        if (match(p, TOKEN_MINUS_MINUS)) return make_inc_dec(loc, expr, "-");
    }

    /* Index access: can chain: a[i][j] */
    while (match(p, TOKEN_LBRACKET)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *index = parse_expression(p);
        consume(p, TOKEN_RBRACKET, "Expected ']'");
        expr = ast_create_index_access(loc, expr, index);
    }
    return expr;
}

static ExprNode *parse_primary(Parser *p) {
    Token *t = cur_tok(p); SourceLocation loc = t->location;
    if (match(p, TOKEN_TRUE))  return ast_create_literal(loc, value_bool(true));
    if (match(p, TOKEN_FALSE)) return ast_create_literal(loc, value_bool(false));
    if (check(p, TOKEN_INT))   { int64_t v = t->value.int_value; p->current++; return ast_create_literal(loc, value_int(v)); }
    if (check(p, TOKEN_FLOAT)) { double v = t->value.float_value; p->current++; return ast_create_literal(loc, value_float(v)); }
    if (check(p, TOKEN_STRING)) {
        char *s = ocl_strdup(t->value.string_value ? t->value.string_value : ""); p->current++;
        return ast_create_literal(loc, value_string(s));
    }
    if (check(p, TOKEN_CHAR)) {
        char c = (t->value.string_value && t->value.string_length > 0) ? t->value.string_value[0] : '\0';
        p->current++; return ast_create_literal(loc, value_char(c));
    }
    if (check(p, TOKEN_IDENTIFIER)) { char *name = dup_lexeme(t); p->current++; return ast_create_identifier(loc, name); }
    if (match(p, TOKEN_LPAREN)) { ExprNode *e = parse_expression(p); consume(p, TOKEN_RPAREN, "Expected ')'"); return e; }

    /* Array literal: [ expr, expr, ... ] */
    if (match(p, TOKEN_LBRACKET)) {
        ExprNode **elems = NULL; size_t count = 0;
        while (!check(p, TOKEN_RBRACKET) && !at_end(p)) {
            elems = ocl_realloc(elems, (count+1)*sizeof(ExprNode*));
            elems[count++] = parse_expression(p);
            if (!match(p, TOKEN_COMMA)) break;
        }
        consume(p, TOKEN_RBRACKET, "Expected ']' after array literal");
        return ast_create_array_literal(loc, elems, count);
    }

    error_add(p->errors, ERROR_PARSER, loc, "Unexpected token '%s' in expression", t->lexeme ? t->lexeme : "?");
    return NULL;
}

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

static bool is_c_style_var_decl(Parser *p) {
    Token *t = cur_tok(p);
    if (t->type != TOKEN_IDENTIFIER) return false;
    if (!is_type_name(t->lexeme)) return false;
    size_t saved = p->current; p->current++;
    Token *nxt = cur_tok(p);
    bool result = (nxt->type == TOKEN_IDENTIFIER);
    p->current = saved;
    return result;
}

/* Parse the else/else-if chain after a then-block.
   Returns: NULL (no else), AST_IF_STMT (else if ...), or AST_BLOCK (plain else). */
static ASTNode *parse_else_chain(Parser *p) {
    if (!match(p, TOKEN_ELSE)) return NULL;
    if (check(p, TOKEN_IF)) {
        /* else if — parse as a full if-statement (creates proper chain) */
        return parse_statement(p);
    }
    /* plain else { ... } */
    return (ASTNode *)parse_block(p);
}

static ASTNode *parse_statement(Parser *p) {
    Token *t = cur_tok(p); SourceLocation loc = t->location;

    /* Import <name.ext> */
    if (match(p, TOKEN_IMPORT)) {
        consume(p, TOKEN_LESS, "Expected '<' after Import");
        char name_buf[256] = {0};
        if (check(p, TOKEN_IDENTIFIER)) { strcat(name_buf, cur_tok(p)->lexeme); p->current++; }
        if (match(p, TOKEN_DOT) && check(p, TOKEN_IDENTIFIER)) { strcat(name_buf, "."); strcat(name_buf, cur_tok(p)->lexeme); p->current++; }
        consume(p, TOKEN_GREATER, "Expected '>'"); match(p, TOKEN_SEMICOLON);
        ImportNode *n = ocl_malloc(sizeof(ImportNode));
        n->base.type = AST_IMPORT; n->base.location = loc; n->filename = ocl_strdup(name_buf);

        /* ── Import resolution ─────────────────────────────────────────
           Look for a stdlib header in the same directory as the source,
           then in a built-in search path.  Recognised extensions:
             .sxh  (OCL standard header)
             .ocl  (OCL source)
           The file is read and pre-pended to the token stream by
           re-lexing it in-place.  For this we return a sentinel and
           the caller (parser_parse) handles it.
        ─────────────────────────────────────────────────────────────── */
        n->base.type = AST_IMPORT;
        return (ASTNode *)n;
    }

    /* declare name : Type  — forward-declares a variable type to the checker */
    if (match(p, TOKEN_DECLARE)) {
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected identifier after 'declare'");
        char *name = dup_lexeme(&name_tok);
        TypeNode *type = NULL;
        if (match(p, TOKEN_COLON)) {
            type = parse_type(p);
        } else {
            type = ast_create_type(TYPE_UNKNOWN, 0);
        }
        match(p, TOKEN_SEMICOLON);
        DeclareNode *dn = ocl_malloc(sizeof(DeclareNode));
        dn->base.type = AST_DECLARE; dn->base.location = loc;
        dn->name = name; dn->type = type;
        return (ASTNode *)dn;
    }

    /* Let name : Type = expr */
    if (match(p, TOKEN_LET)) {
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        char *name = dup_lexeme(&name_tok);
        consume(p, TOKEN_COLON, "Expected ':' after variable name");
        TypeNode *type = parse_type(p); ExprNode *init = NULL;
        if (match(p, TOKEN_EQUAL)) init = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return ast_create_var_decl(loc, name, type, init);
    }

    /* C-style: Type name = expr */
    if (is_c_style_var_decl(p)) {
        TypeNode *type = parse_type(p);
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        char *name = dup_lexeme(&name_tok); ExprNode *init = NULL;
        if (match(p, TOKEN_EQUAL)) init = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return ast_create_var_decl(loc, name, type, init);
    }

    /* if / else-if / else  ─── now produces a flat chain, no synthetic blocks */
    if (match(p, TOKEN_IF)) {
        consume(p, TOKEN_LPAREN, "Expected '(' after if");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')' after if condition");
        BlockNode *then_b = parse_block(p);
        ASTNode *else_next = parse_else_chain(p);
        return ast_create_if_stmt(loc, cond, then_b, else_next);
    }

    /* while */
    if (match(p, TOKEN_WHILE)) {
        consume(p, TOKEN_LPAREN, "Expected '('");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')'");
        BlockNode *body = parse_block(p);
        LoopNode *lp = ocl_malloc(sizeof(LoopNode));
        lp->base.type = AST_WHILE_LOOP; lp->base.location = loc; lp->is_for = false;
        lp->init = NULL; lp->condition = cond; lp->increment = NULL; lp->body = body;
        return (ASTNode *)lp;
    }

    /* for */
    if (match(p, TOKEN_FOR)) {
        consume(p, TOKEN_LPAREN, "Expected '('");
        ASTNode *init = NULL;
        if (match(p, TOKEN_LET)) {
            Token n_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
            char *vname = dup_lexeme(&n_tok); consume(p, TOKEN_COLON, "Expected ':'");
            TypeNode *vtype = parse_type(p); ExprNode *vinit = NULL;
            if (match(p, TOKEN_EQUAL)) vinit = parse_expression(p);
            init = ast_create_var_decl(loc, vname, vtype, vinit);
        } else if (is_c_style_var_decl(p)) {
            TypeNode *vtype = parse_type(p);
            Token n_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
            char *vname = dup_lexeme(&n_tok); ExprNode *vinit = NULL;
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
        LoopNode *lp = ocl_malloc(sizeof(LoopNode));
        lp->base.type = AST_FOR_LOOP; lp->base.location = loc; lp->is_for = true;
        lp->init = init; lp->condition = cond; lp->increment = incr; lp->body = body;
        return (ASTNode *)lp;
    }

    if (match(p, TOKEN_RETURN)) {
        ExprNode *val = NULL;
        if (!check(p, TOKEN_SEMICOLON) && !check(p, TOKEN_RBRACE) && !at_end(p)) val = parse_expression(p);
        match(p, TOKEN_SEMICOLON);
        return ast_create_return(loc, val);
    }
    if (match(p, TOKEN_BREAK)) {
        match(p, TOKEN_SEMICOLON);
        ASTNode *n = ocl_malloc(sizeof(ASTNode)); n->type = AST_BREAK; n->location = loc; return n;
    }
    if (match(p, TOKEN_CONTINUE)) {
        match(p, TOKEN_SEMICOLON);
        ASTNode *n = ocl_malloc(sizeof(ASTNode)); n->type = AST_CONTINUE; n->location = loc; return n;
    }
    ExprNode *expr = parse_expression(p);
    if (!expr) { p->current++; return NULL; }
    match(p, TOKEN_SEMICOLON);
    return (ASTNode *)expr;
}

Parser *parser_create(Token *tokens, size_t token_count, const char *filename, ErrorCollector *errors) {
    Parser *p = ocl_malloc(sizeof(Parser));
    p->tokens = tokens; p->token_count = token_count; p->current = 0;
    p->filename = filename; p->errors = errors;
    return p;
}

void parser_free(Parser *p) { ocl_free(p); }

/* ── Import file resolution ─────────────────────────────────────────────
   Search paths (in order):
     1. Same directory as the source file being parsed
     2. ./stdlib_headers/
     3. <executable dir>/stdlib_headers/
   File extensions tried: as-is, then .ocl, then .sxh
──────────────────────────────────────────────────────────────────────── */
static char *find_import_file(const char *importing_file, const char *import_name) {
    char bases[4][512];
    int nb = 0;

    /* 1: same directory as the importing file */
    if (importing_file) {
        const char *slash = strrchr(importing_file, '/');
        if (!slash) slash = strrchr(importing_file, '\\');
        size_t dir_len = slash ? (size_t)(slash - importing_file + 1) : 0;
        if (dir_len + strlen(import_name) + 5 < 512) {
            snprintf(bases[nb++], 512, "%.*s%s", (int)dir_len, importing_file, import_name);
        }
    }

    /* 2: ./ocl_headers/<name> */
    snprintf(bases[nb++], 512, "ocl_headers/%s", import_name);

    /* 3: ./stdlib_headers/<name> */
    snprintf(bases[nb++], 512, "stdlib_headers/%s", import_name);

    /* 4: plain name (relative to cwd) */
    snprintf(bases[nb++], 512, "%s", import_name);

    static const char *exts[] = { "", ".ocl", ".sxh" };
    static const int   next   = sizeof(exts) / sizeof(exts[0]);

    for (int i = 0; i < nb; i++) {
        for (int e = 0; e < next; e++) {
            char candidate[520];
            snprintf(candidate, sizeof(candidate), "%s%s", bases[i], exts[e]);
            FILE *f = fopen(candidate, "r");
            if (f) { fclose(f); return ocl_strdup(candidate); }
        }
    }
    return NULL;
}

static char *read_file_import(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = ocl_malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f); buf[rd] = '\0';
    fclose(f); return buf;
}

ProgramNode *parser_parse(Parser *p) {
    ProgramNode *prog = ocl_malloc(sizeof(ProgramNode));
    prog->base.type = AST_PROGRAM; prog->base.location = (SourceLocation){1,1,p->filename};
    prog->nodes = NULL; prog->node_count = 0;

    while (!at_end(p)) {
        /* Function declaration */
        if (check(p, TOKEN_FUNC)) {
            p->current++;
            SourceLocation loc = prev_tok(p)->location;
            TypeNode *ret = ast_create_type(TYPE_VOID, 0);
            if (check(p, TOKEN_IDENTIFIER)) {
                char *maybe_type = cur_tok(p)->lexeme;
                if (is_type_name(maybe_type)) { ocl_free(ret); ret = parse_type(p); }
            }
            Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected function name");
            char *fname = dup_lexeme(&name_tok);
            consume(p, TOKEN_LPAREN, "Expected '(' after function name");
            ParamNode **params = NULL; size_t param_count = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    Token pname_tok = consume(p, TOKEN_IDENTIFIER, "Expected parameter name");
                    char *pname = dup_lexeme(&pname_tok);
                    consume(p, TOKEN_COLON, "Expected ':' after parameter name");
                    TypeNode *ptype = parse_type(p);
                    params = ocl_realloc(params, (param_count+1)*sizeof(ParamNode *));
                    params[param_count++] = ast_create_param(pname, ptype, pname_tok.location);
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "Expected ')'");
            BlockNode *body = parse_block(p);
            ASTNode *fn = ast_create_func_decl(loc, fname, ret, params, param_count, body);
            prog->nodes = ocl_realloc(prog->nodes, (prog->node_count+1)*sizeof(ASTNode *));
            prog->nodes[prog->node_count++] = fn;
            continue;
        }

        /* Import — try to resolve and inline */
        if (check(p, TOKEN_IMPORT)) {
            ASTNode *import_node = parse_statement(p);  /* consumes the Import statement */
            if (!import_node) continue;
            ImportNode *imp = (ImportNode *)import_node;

            char *resolved = find_import_file(p->filename, imp->filename);
            if (resolved) {
                char *src = read_file_import(resolved);
                if (src) {
                    /* Lex the imported file and inject its tokens */
                    Lexer *sub_lex = lexer_create(src, resolved);
                    size_t sub_count = 0;
                    Token *sub_tokens = lexer_tokenize_all(sub_lex, &sub_count);
                    lexer_free(sub_lex);

                    /* Build a temporary sub-parser and parse all top-level nodes */
                    Parser *sub = parser_create(sub_tokens, sub_count, resolved, p->errors);
                    ProgramNode *sub_prog = parser_parse(sub);
                    parser_free(sub);
                    tokens_free(sub_tokens, sub_count);
                    ocl_free(src);

                    /* Merge sub-program nodes into main program */
                    if (sub_prog) {
                        for (size_t i = 0; i < sub_prog->node_count; i++) {
                            prog->nodes = ocl_realloc(prog->nodes, (prog->node_count+1)*sizeof(ASTNode *));
                            prog->nodes[prog->node_count++] = sub_prog->nodes[i];
                            sub_prog->nodes[i] = NULL; /* transferred ownership */
                        }
                        ocl_free(sub_prog->nodes);
                        ocl_free(sub_prog);
                    }
                }
                ocl_free(resolved);
            } else {
                error_add(p->errors, ERROR_PARSER, import_node->location,
                          "Import not found: '%s'", imp->filename);
            }


            /* Keep the Import node itself (for the type checker / codegen to ignore) */
            prog->nodes = ocl_realloc(prog->nodes, (prog->node_count+1)*sizeof(ASTNode *));
            prog->nodes[prog->node_count++] = import_node;
            continue;
        }

        ASTNode *stmt = parse_statement(p);
        if (stmt) {
            prog->nodes = ocl_realloc(prog->nodes, (prog->node_count+1)*sizeof(ASTNode *));
            prog->nodes[prog->node_count++] = stmt;
        }
    }
    return prog;
}