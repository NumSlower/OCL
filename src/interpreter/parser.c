#include "parser.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define TokenType WindowsTokenType
#include <windows.h>
#undef TokenType
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

struct ParseState {
    char  **import_stack;
    size_t  import_depth;
    size_t  import_capacity;
};

static ParseState *parse_state_create(void) {
    ParseState *state = ocl_malloc(sizeof(ParseState));
    state->import_stack = NULL;
    state->import_depth = 0;
    state->import_capacity = 0;
    return state;
}

static void parse_state_free(ParseState *state) {
    if (!state) return;
    for (size_t i = 0; i < state->import_depth; i++)
        ocl_free(state->import_stack[i]);
    ocl_free(state->import_stack);
    ocl_free(state);
}

static void parse_state_push(ParseState *state, const char *path) {
    if (!state || !path) return;
    if (state->import_depth >= state->import_capacity) {
        state->import_capacity = state->import_capacity ? state->import_capacity * 2 : 8;
        state->import_stack = ocl_realloc(state->import_stack,
                                          state->import_capacity * sizeof(char *));
    }
    state->import_stack[state->import_depth++] = ocl_strdup(path);
}

static void parse_state_pop(ParseState *state) {
    if (!state || state->import_depth == 0) return;
    ocl_free(state->import_stack[--state->import_depth]);
}

static bool parse_state_contains(ParseState *state, const char *path) {
    if (!state || !path) return false;
    for (size_t i = 0; i < state->import_depth; i++)
        if (!strcmp(state->import_stack[i], path)) return true;
    return false;
}

#define OCL_IMPORT_PATH_MAX 4096
#define OCL_IMPORT_BASE_MAX 32
#define OCL_IMPORT_ROOT_MAX 8

static bool copy_path(char *out, size_t out_size, const char *value) {
    int n;

    if (!out || out_size == 0 || !value) return false;
    n = snprintf(out, out_size, "%s", value);
    return n >= 0 && (size_t)n < out_size;
}

static bool dirname_in_place(char *path) {
    char *slash;

    if (!path || !path[0]) return false;
    slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    if (!slash) return false;
    if (slash == path) {
        path[1] = '\0';
        return true;
    }

    *slash = '\0';
    return true;
}

static bool append_import_base(char bases[][OCL_IMPORT_PATH_MAX], int *count,
                               const char *prefix, const char *suffix) {
    int n;

    if (!bases || !count || !suffix || *count >= OCL_IMPORT_BASE_MAX)
        return false;

    if (!prefix || !prefix[0]) {
        n = snprintf(bases[*count], OCL_IMPORT_PATH_MAX, "%s", suffix);
    } else if (prefix[strlen(prefix) - 1] == '/' || prefix[strlen(prefix) - 1] == '\\') {
        n = snprintf(bases[*count], OCL_IMPORT_PATH_MAX, "%s%s", prefix, suffix);
    } else {
        n = snprintf(bases[*count], OCL_IMPORT_PATH_MAX, "%s/%s", prefix, suffix);
    }

    if (n < 0 || (size_t)n >= OCL_IMPORT_PATH_MAX)
        return false;

    (*count)++;
    return true;
}

static void append_owned_text(char **buffer, size_t *len, size_t *cap, const char *text) {
    size_t text_len;
    size_t new_cap;

    if (!buffer || !len || !cap || !text) return;

    text_len = strlen(text);
    if (*len + text_len + 1 > *cap) {
        new_cap = *cap ? *cap : 32;
        while (*len + text_len + 1 > new_cap)
            new_cap *= 2;
        *buffer = ocl_realloc(*buffer, new_cap);
        *cap = new_cap;
    }

    memcpy(*buffer + *len, text, text_len);
    *len += text_len;
    (*buffer)[*len] = '\0';
}

static bool at_end(Parser *p);
static Token *cur_tok(Parser *p);
static Token *prev_tok(Parser *p);
static bool check(Parser *p, TokenType t);
static Token consume(Parser *p, TokenType t, const char *msg);

static char *parse_import_name(Parser *p) {
    char *name = NULL;
    size_t len = 0;
    size_t cap = 0;
    bool expect_segment = true;

    while (!check(p, TOKEN_GREATER) && !at_end(p)) {
        if (!expect_segment) {
            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, cur_tok(p)->location,
                      "Expected '>' after import name");
            break;
        }

        Token part = consume(p, TOKEN_IDENTIFIER, "Expected import name");
        append_owned_text(&name, &len, &cap, part.lexeme ? part.lexeme : "");
        expect_segment = false;

        if (check(p, TOKEN_DOT)) {
            p->current++;
            append_owned_text(&name, &len, &cap, ".");
            expect_segment = true;
        }
    }

    if (expect_segment && name) {
        error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, prev_tok(p)->location,
                  "Import name cannot end with '.'");
    }

    if (!name)
        name = ocl_strdup("");
    return name;
}

static bool append_import_subpath(char bases[][OCL_IMPORT_PATH_MAX], int *count,
                                  const char *root, const char *dir,
                                  const char *name) {
    char prefix[OCL_IMPORT_PATH_MAX];

    if (dir && dir[0]) {
        if (!append_import_base(bases, count, root, dir))
            return false;
        if (!copy_path(prefix, sizeof(prefix), bases[*count - 1])) {
            (*count)--;
            return false;
        }
        (*count)--;
        return append_import_base(bases, count, prefix, name);
    }

    return append_import_base(bases, count, root, name);
}

static bool resolve_executable_dir(char *out, size_t out_size) {
    char exe_path[OCL_IMPORT_PATH_MAX];

#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
    if (len == 0 || len >= (DWORD)sizeof(exe_path))
        return false;
    exe_path[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0)
        return false;
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0)
        return false;
    exe_path[len] = '\0';
#endif

    if (!dirname_in_place(exe_path))
        return false;
    return copy_path(out, out_size, exe_path);
}

static int resolve_runtime_roots(char roots[][OCL_IMPORT_PATH_MAX]) {
    const char *env_root = getenv("OCL_PROJECT_ROOT");
    int count = 0;

    if (!roots) return 0;

    if (env_root && env_root[0]) {
        if (copy_path(roots[count], OCL_IMPORT_PATH_MAX, env_root))
            count++;
        return count;
    }

    if (resolve_executable_dir(roots[count], OCL_IMPORT_PATH_MAX)) {
        char current[OCL_IMPORT_PATH_MAX];

        if (!copy_path(current, sizeof(current), roots[count]))
            return count;
        count++;

        while (count < OCL_IMPORT_ROOT_MAX) {
            char parent[OCL_IMPORT_PATH_MAX];

            if (!copy_path(parent, sizeof(parent), current) ||
                !dirname_in_place(parent) ||
                strcmp(parent, current) == 0)
                break;

            if (!copy_path(roots[count], OCL_IMPORT_PATH_MAX, parent))
                break;

            if (!copy_path(current, sizeof(current), parent))
                break;
            count++;
        }
    }

    return count;
}

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

static Token *peek_tok(Parser *p, size_t offset) {
    size_t i = p->current;
    while (i < p->token_count && p->tokens[i].type == TOKEN_NEWLINE) i++;
    while (offset > 0 && i < p->token_count) {
        i++;
        while (i < p->token_count && p->tokens[i].type == TOKEN_NEWLINE) i++;
        offset--;
    }
    return &p->tokens[i < p->token_count ? i : p->token_count - 1];
}

static bool check(Parser *p, TokenType t) { return cur_tok(p)->type == t; }

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { p->current++; return true; }
    return false;
}

static Token consume(Parser *p, TokenType t, const char *msg) {
    if (check(p, t)) { Token tok = *cur_tok(p); p->current++; return tok; }
    Token *c = cur_tok(p); SourceLocation loc = c->location;
    if (p->errors) error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc, "%s (got '%s')", msg, c->lexeme ? c->lexeme : "?");
    else fprintf(stderr, "PARSE ERROR [%d:%d]: %s\n", loc.line, loc.column, msg);
    return *c;
}

static SourceLocation previous_token_end_location(Parser *p) {
    Token *prev = prev_tok(p);
    SourceLocation loc = prev ? prev->location : LOC_NONE;

    if (!prev)
        return loc;

    if (loc.column < 1)
        loc.column = 1;

    if (prev->lexeme_length > 0)
        loc.column += (int)prev->lexeme_length;

    return loc;
}

static Token consume_semicolon(Parser *p, const char *msg) {
    SourceLocation loc = previous_token_end_location(p);

    if (check(p, TOKEN_SEMICOLON)) {
        Token tok = *cur_tok(p);
        p->current++;
        return tok;
    }

    Token *c = cur_tok(p);
    if (loc.line <= 0)
        loc = c->location;

    if (p->errors) {
        error_add_with_related(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc, c->location,
                               "%s (got '%s')", msg, c->lexeme ? c->lexeme : "?");
    } else {
        fprintf(stderr, "PARSE ERROR [%d:%d]: %s\n", loc.line, loc.column, msg);
    }

    return *c;
}

static char *dup_lexeme(const Token *t) { return !t->lexeme ? ocl_strdup("") : ocl_strdup(t->lexeme); }

static ASTNode  *parse_statement(Parser *p);
static ExprNode *parse_expression(Parser *p);
static ExprNode *parse_assignment(Parser *p);
static ExprNode *parse_conditional(Parser *p);
static ExprNode *parse_null_coalesce(Parser *p);
static ExprNode *parse_or(Parser *p);
static ExprNode *parse_and(Parser *p);
static ExprNode *parse_bitwise_or(Parser *p);
static ExprNode *parse_bitwise_xor(Parser *p);
static ExprNode *parse_bitwise_and(Parser *p);
static ExprNode *parse_equality(Parser *p);
static ExprNode *parse_comparison(Parser *p);
static ExprNode *parse_shift(Parser *p);
static ExprNode *parse_addition(Parser *p);
static ExprNode *parse_multiplication(Parser *p);
static ExprNode *parse_unary(Parser *p);
static ExprNode *parse_postfix(Parser *p);
static ExprNode *parse_primary(Parser *p);
static BlockNode *parse_block(Parser *p);
static TypeNode  *parse_type(Parser *p);
static ExprNode *parse_embedded_expression(Parser *parent, const char *source, SourceLocation loc);
static ExprNode *parse_interpolated_string(Parser *p, const Token *token);
static ExprNode *make_to_string_call(SourceLocation loc, ExprNode *expr);
static bool is_anon_func_start(Parser *p);
static void skip_brace_block(Parser *p);
static ASTNode *parse_top_level_function_decl(Parser *p);

static bool looks_like_typed_name(Parser *p) {
    return check(p, TOKEN_IDENTIFIER) && peek_tok(p, 1)->type == TOKEN_IDENTIFIER;
}

static TypeNode *make_function_type(TypeNode **param_types, size_t param_count, TypeNode *return_type) {
    TypeNode *type = ast_create_type(TYPE_FUNCTION);
    type->param_types = param_types;
    type->param_count = param_count;
    type->return_type = return_type;
    return type;
}

static bool token_is_func_type_name(const Token *token) {
    return token &&
           token->type == TOKEN_IDENTIFIER &&
           token->lexeme &&
           strcmp(token->lexeme, "Func") == 0;
}

static TypeNode *parse_type(Parser *p) {
    Token *t = cur_tok(p);
    if (token_is_func_type_name(t)) {
        TypeNode **param_types = NULL;
        size_t param_count = 0;

        p->current++;
        consume(p, TOKEN_LPAREN, "Expected '(' after Func");
        if (!check(p, TOKEN_RPAREN)) {
            do {
                param_types = ocl_realloc(param_types, (param_count + 1) * sizeof(TypeNode *));
                param_types[param_count++] = parse_type(p);
            } while (match(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RPAREN, "Expected ')' after function type parameters");
        consume(p, TOKEN_ARROW, "Expected '->' after function type parameters");
        return make_function_type(param_types, param_count, parse_type(p));
    }

    if (t->type != TOKEN_IDENTIFIER) {
        error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, t->location, "Expected type name");
        return ast_create_type(TYPE_UNKNOWN);
    }
    char *name = dup_lexeme(t);
    TypeNode *tn = NULL;

    p->current++;

    if (!strcmp(name, "Int"))               tn = ast_create_integer_type(INTEGER_KIND_GENERIC_INT);
    else if (!strcmp(name, "ichar"))        tn = ast_create_integer_type(INTEGER_KIND_ICHAR);
    else if (!strcmp(name, "short"))        tn = ast_create_integer_type(INTEGER_KIND_SHORT);
    else if (!strcmp(name, "int") ||
             !strcmp(name, "int32"))        tn = ast_create_integer_type(INTEGER_KIND_INT);
    else if (!strcmp(name, "long") ||
             !strcmp(name, "int64"))        tn = ast_create_integer_type(INTEGER_KIND_LONG);
    else if (!strcmp(name, "int128"))       tn = ast_create_integer_type(INTEGER_KIND_INT128);
    else if (!strcmp(name, "iptr"))         tn = ast_create_integer_type(INTEGER_KIND_IPTR);
    else if (!strcmp(name, "isz"))          tn = ast_create_integer_type(INTEGER_KIND_ISZ);
    else if (!strcmp(name, "char"))         tn = ast_create_integer_type(INTEGER_KIND_CHAR);
    else if (!strcmp(name, "ushort"))       tn = ast_create_integer_type(INTEGER_KIND_USHORT);
    else if (!strcmp(name, "uint"))         tn = ast_create_integer_type(INTEGER_KIND_UINT);
    else if (!strcmp(name, "ulong"))        tn = ast_create_integer_type(INTEGER_KIND_ULONG);
    else if (!strcmp(name, "uint128"))      tn = ast_create_integer_type(INTEGER_KIND_UINT128);
    else if (!strcmp(name, "uptr"))         tn = ast_create_integer_type(INTEGER_KIND_UPTR);
    else if (!strcmp(name, "usz"))          tn = ast_create_integer_type(INTEGER_KIND_USZ);
    else if (!strcmp(name, "float") ||
             !strcmp(name, "Float"))        tn = ast_create_type(TYPE_FLOAT);
    else if (!strcmp(name, "string") ||
             !strcmp(name, "String"))       tn = ast_create_type(TYPE_STRING);
    else if (!strcmp(name, "bool") ||
             !strcmp(name, "Bool"))         tn = ast_create_type(TYPE_BOOL);
    else if (!strcmp(name, "Char"))         tn = ast_create_type(TYPE_CHAR);
    else if (!strcmp(name, "void") ||
             !strcmp(name, "Void"))         tn = ast_create_type(TYPE_VOID);
    else if (!strcmp(name, "array") ||
             !strcmp(name, "Array"))        tn = ast_create_type(TYPE_ARRAY);
    else                                    tn = ast_create_type_named(TYPE_STRUCT, name);

    ocl_free(name);

    if (match(p, TOKEN_LBRACKET)) {
        consume(p, TOKEN_RBRACKET, "Expected ']'");
    }
    return tn;
}

static ExprNode *make_to_string_call(SourceLocation loc, ExprNode *expr) {
    ExprNode **args = ocl_malloc(sizeof(ExprNode *));
    args[0] = expr;
    return ast_create_call(loc, ast_create_identifier(loc, ocl_strdup("toString")), args, 1);
}

static ExprNode *parse_embedded_expression(Parser *parent, const char *source, SourceLocation loc) {
    if (!parent || !source) return NULL;

    Lexer *lexer = lexer_create(source, parent->filename);
    size_t token_count = 0;
    Token *tokens = lexer_tokenize_all(lexer, &token_count);
    lexer_free(lexer);

    Parser *embedded = parser_create(tokens, token_count, parent->filename, parent->errors);
    if (embedded->state) parse_state_free(embedded->state);
    embedded->state = parent->state;
    embedded->owns_state = false;

    ExprNode *expr = parse_expression(embedded);
    if (!expr || !check(embedded, TOKEN_EOF)) {
        error_add(parent->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
                  "Invalid interpolation expression");
        if (expr) ast_free((ASTNode *)expr);
        expr = ast_create_literal(loc, value_null());
    }

    parser_free(embedded);
    tokens_free(tokens, token_count);
    return expr;
}

static ExprNode *parse_interpolated_string(Parser *p, const Token *token) {
    const char *raw = token && token->raw_string_value ? token->raw_string_value : "";
    size_t len = token ? token->raw_string_length : 0;
    size_t segment_start = 0;
    ExprNode *result = NULL;

    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '$' && i + 1 < len && raw[i + 1] == '{') {
            if (i > segment_start) {
                char *segment = ocl_malloc(i - segment_start + 1);
                memcpy(segment, raw + segment_start, i - segment_start);
                segment[i - segment_start] = '\0';
                ExprNode *literal = ast_create_literal(token->location, value_string(segment));
                result = result ? ast_create_binary_op(token->location, result, "+", literal) : literal;
            }

            size_t expr_start = i + 2;
            size_t depth = 1;
            size_t j = expr_start;
            while (j < len && depth > 0) {
                if (raw[j] == '{') depth++;
                else if (raw[j] == '}') depth--;
                if (depth > 0) j++;
            }

            if (depth != 0) {
                error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, token->location,
                          "Unterminated interpolation segment");
                break;
            }

            char *expr_source = ocl_malloc(j - expr_start + 1);
            memcpy(expr_source, raw + expr_start, j - expr_start);
            expr_source[j - expr_start] = '\0';

            ExprNode *expr = parse_embedded_expression(p, expr_source, token->location);
            ocl_free(expr_source);
            expr = make_to_string_call(token->location, expr);
            result = result ? ast_create_binary_op(token->location, result, "+", expr) : expr;
            i = j;
            segment_start = j + 1;
        }
    }

    if (segment_start < len || !result) {
        char *segment = ocl_malloc(len - segment_start + 1);
        memcpy(segment, raw + segment_start, len - segment_start);
        segment[len - segment_start] = '\0';
        ExprNode *literal = ast_create_literal(token->location, value_string(segment));
        result = result ? ast_create_binary_op(token->location, result, "+", literal) : literal;
    }

    return result;
}

static bool is_anon_func_start(Parser *p) {
    return check(p, TOKEN_FUNC) && peek_tok(p, 1)->type == TOKEN_LPAREN;
}

static void skip_unsupported_extern_decl(Parser *p) {
    if (!match(p, TOKEN_EXTERN))
        return;

    while (!at_end(p)) {
        if (check(p, TOKEN_LBRACE)) {
            skip_brace_block(p);
            break;
        }
        if (check(p, TOKEN_SEMICOLON)) {
            p->current++;
            break;
        }
        p->current++;
    }
}

static ASTNode *parse_top_level_function_decl(Parser *p) {
    bool is_extern = false;
    char *extern_library = NULL;
    SourceLocation loc;
    TypeNode *ret = NULL;
    ParamNode **params = NULL;
    size_t param_count = 0;
    BlockNode *body = NULL;

    if (match(p, TOKEN_EXTERN)) {
        Token lib_tok;

        is_extern = true;
        lib_tok = consume(p, TOKEN_STRING, "Expected library string after extern");
        extern_library = ocl_strdup(lib_tok.value.string_value ? lib_tok.value.string_value : "");
        consume(p, TOKEN_FUNC, "Expected 'func' after extern library");
        loc = lib_tok.location;
    } else {
        Token func_tok = consume(p, TOKEN_FUNC, "Expected 'func'");
        loc = func_tok.location;
    }

    ret = ast_create_type(TYPE_VOID);
    if (token_is_func_type_name(cur_tok(p)) ||
        !(check(p, TOKEN_IDENTIFIER) && peek_tok(p, 1)->type == TOKEN_LPAREN)) {
        ocl_free(ret);
        ret = parse_type(p);
    }

    Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected function name");
    char *fname = dup_lexeme(&name_tok);
    consume(p, TOKEN_LPAREN, "Expected '(' after function name");

    if (!check(p, TOKEN_RPAREN)) {
        do {
            Token pname_tok = consume(p, TOKEN_IDENTIFIER, "Expected parameter name");
            char *pname = dup_lexeme(&pname_tok);
            consume(p, TOKEN_COLON, "Expected ':' after parameter name");
            TypeNode *ptype = parse_type(p);
            params = ocl_realloc(params, (param_count + 1) * sizeof(ParamNode *));
            params[param_count++] = ast_create_param(pname, ptype, pname_tok.location);
        } while (match(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RPAREN, "Expected ')'");

    if (is_extern) {
        if (check(p, TOKEN_LBRACE)) {
            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, cur_tok(p)->location,
                      "Extern functions must end with ';' and cannot have a body");
            skip_brace_block(p);
        } else {
            consume_semicolon(p, "Expected ';' after extern function declaration");
        }
        return ast_create_func_decl(loc, fname, ret, params, param_count, NULL, true, extern_library);
    }

    body = parse_block(p);
    return ast_create_func_decl(loc, fname, ret, params, param_count, body, false, NULL);
}

static ExprNode *parse_expression(Parser *p) { return parse_assignment(p); }

static bool is_assignable_expr(const ExprNode *expr) {
    if (!expr) return false;
    return expr->base.type == AST_IDENTIFIER ||
           expr->base.type == AST_INDEX_ACCESS ||
           expr->base.type == AST_FIELD_ACCESS;
}

static ExprNode *make_inc_dec(SourceLocation loc, ExprNode *target, const char *op) {
    ExprNode *target_copy = ast_clone_expr(target);
    if (!target_copy) {
        ast_free((ASTNode *)target);
        return NULL;
    }
    ExprNode *one = ast_create_literal(loc, value_int(1));
    ExprNode *arith = ast_create_binary_op(loc, target_copy, op, one);
    return ast_create_binary_op(loc, target, "=", arith);
}

static ExprNode *parse_assignment(Parser *p) {
    ExprNode *lhs = parse_conditional(p); if (!lhs) return NULL;

    if (match(p, TOKEN_EQUAL)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *rhs = parse_assignment(p);
        return ast_create_binary_op(loc, lhs, "=", rhs);
    }

    /*
     * Compound assignment: lhs op= rhs  →  lhs = lhs op rhs
     *
     * The arithmetic expression needs its own copy of the left-hand side
     * subtree so both sides can be freed independently.
     */
    static const struct { TokenType tok; const char *op; } compound[] = {
        { TOKEN_PLUS_EQUAL,    "+" },
        { TOKEN_MINUS_EQUAL,   "-" },
        { TOKEN_STAR_EQUAL,    "*" },
        { TOKEN_SLASH_EQUAL,   "/" },
        { TOKEN_PERCENT_EQUAL, "%" },
        { TOKEN_AMPERSAND_EQUAL, "&" },
        { TOKEN_PIPE_EQUAL, "|" },
        { TOKEN_CARET_EQUAL, "^" },
        { TOKEN_SHIFT_LEFT_EQUAL, "<<" },
        { TOKEN_SHIFT_RIGHT_EQUAL, ">>" },
        { (TokenType)0, NULL }
    };

    for (int i = 0; compound[i].op; i++) {
        if (match(p, compound[i].tok)) {
            SourceLocation loc = prev_tok(p)->location;
            ExprNode *rhs = parse_assignment(p);

            if (is_assignable_expr(lhs)) {
                ExprNode *lhs_copy = ast_clone_expr(lhs);
                if (!lhs_copy) {
                    error_add(p->errors, ERRK_LOGIC, ERROR_PARSER, loc,
                              "Failed to clone compound-assignment target");
                    ast_free((ASTNode *)rhs);
                    return lhs;
                }
                ExprNode *arith = ast_create_binary_op(loc, lhs_copy, compound[i].op, rhs);
                return ast_create_binary_op(loc, lhs, "=", arith);
            }

            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
                      "Compound assignment '%s=' requires a variable or index expression on the left-hand side",
                      compound[i].op);
            ast_free((ASTNode *)rhs);
            return lhs;
        }
    }

    return lhs;
}

static ExprNode *parse_conditional(Parser *p) {
    ExprNode *condition = parse_null_coalesce(p);
    if (!condition) return NULL;

    if (!match(p, TOKEN_QUESTION))
        return condition;

    SourceLocation loc = prev_tok(p)->location;
    ExprNode *true_expr = parse_conditional(p);
    consume(p, TOKEN_COLON, "Expected ':' after ternary true branch");
    ExprNode *false_expr = parse_conditional(p);
    return ast_create_ternary(loc, condition, true_expr, false_expr);
}

static ExprNode *parse_null_coalesce(Parser *p) {
    ExprNode *left = parse_or(p);
    if (!left) return NULL;

    if (match(p, TOKEN_QUESTION_QUESTION)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *right = parse_null_coalesce(p);
        return ast_create_binary_op(loc, left, "??", right);
    }

    return left;
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
    ExprNode *e = parse_bitwise_or(p);
    while (match(p, TOKEN_AMPERSAND_AMPERSAND)) {
        SourceLocation loc = prev_tok(p)->location;
        e = ast_create_binary_op(loc, e, "&&", parse_bitwise_or(p));
    }
    return e;
}

static ExprNode *parse_bitwise_or(Parser *p) {
    ExprNode *e = parse_bitwise_xor(p);
    while (match(p, TOKEN_PIPE)) {
        SourceLocation loc = prev_tok(p)->location;
        e = ast_create_binary_op(loc, e, "|", parse_bitwise_xor(p));
    }
    return e;
}

static ExprNode *parse_bitwise_xor(Parser *p) {
    ExprNode *e = parse_bitwise_and(p);
    while (match(p, TOKEN_CARET)) {
        SourceLocation loc = prev_tok(p)->location;
        e = ast_create_binary_op(loc, e, "^", parse_bitwise_and(p));
    }
    return e;
}

static ExprNode *parse_bitwise_and(Parser *p) {
    ExprNode *e = parse_equality(p);
    while (match(p, TOKEN_AMPERSAND)) {
        SourceLocation loc = prev_tok(p)->location;
        e = ast_create_binary_op(loc, e, "&", parse_equality(p));
    }
    return e;
}

static ExprNode *parse_equality(Parser *p) {
    ExprNode *e = parse_comparison(p);
    for (;;) {
        if (match(p, TOKEN_EQUAL_EQUAL))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "==", parse_comparison(p)); }
        else if (match(p, TOKEN_BANG_EQUAL))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "!=", parse_comparison(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_comparison(Parser *p) {
    ExprNode *e = parse_shift(p);
    for (;;) {
        if (match(p, TOKEN_LESS))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "<",  parse_shift(p)); }
        else if (match(p, TOKEN_LESS_EQUAL))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "<=", parse_shift(p)); }
        else if (match(p, TOKEN_GREATER))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, ">",  parse_shift(p)); }
        else if (match(p, TOKEN_GREATER_EQUAL))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, ">=", parse_shift(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_shift(Parser *p) {
    ExprNode *e = parse_addition(p);
    for (;;) {
        if (match(p, TOKEN_SHIFT_LEFT))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "<<", parse_addition(p)); }
        else if (match(p, TOKEN_SHIFT_RIGHT))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, ">>", parse_addition(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_addition(Parser *p) {
    ExprNode *e = parse_multiplication(p);
    for (;;) {
        if (match(p, TOKEN_PLUS))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "+", parse_multiplication(p)); }
        else if (match(p, TOKEN_MINUS))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "-", parse_multiplication(p)); }
        else break;
    }
    return e;
}

static ExprNode *parse_multiplication(Parser *p) {
    ExprNode *e = parse_unary(p);
    for (;;) {
        if (match(p, TOKEN_STAR))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "*", parse_unary(p)); }
        else if (match(p, TOKEN_SLASH))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "/", parse_unary(p)); }
        else if (match(p, TOKEN_PERCENT))
            { SourceLocation loc = prev_tok(p)->location; e = ast_create_binary_op(loc, e, "%", parse_unary(p)); }
        else break;
    }
    return e;
}

/*
 * make_inc_dec — desugar prefix/postfix ++/-- into "var = var + 1".
 *
 * Takes OWNERSHIP of `var` (it becomes the lhs of the "=" node).
 * Creates a fresh copy of the identifier for the rhs arithmetic operand.
 * PRECONDITION: var->base.type == AST_IDENTIFIER.
 */
static ExprNode *parse_unary(Parser *p) {
    if (match(p, TOKEN_PLUS_PLUS)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *target = parse_postfix(p);
        if (!is_assignable_expr(target)) {
            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc, "Expected assignable expression after '++'");
            ast_free((ASTNode *)target);
            return NULL;
        }
        return make_inc_dec(loc, target, "+");
    }
    if (match(p, TOKEN_MINUS_MINUS)) {
        SourceLocation loc = prev_tok(p)->location;
        ExprNode *target = parse_postfix(p);
        if (!is_assignable_expr(target)) {
            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc, "Expected assignable expression after '--'");
            ast_free((ASTNode *)target);
            return NULL;
        }
        return make_inc_dec(loc, target, "-");
    }
    if (match(p, TOKEN_BANG)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type = AST_UNARY_OP; u->base.location = loc;
        u->operator = "!"; u->operand = parse_unary(p);
        return (ExprNode *)u;
    }
    if (match(p, TOKEN_MINUS)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type = AST_UNARY_OP; u->base.location = loc;
        u->operator = "-"; u->operand = parse_unary(p);
        return (ExprNode *)u;
    }
    if (match(p, TOKEN_TILDE)) {
        SourceLocation loc = prev_tok(p)->location;
        UnaryOpNode *u = ocl_malloc(sizeof(UnaryOpNode));
        u->base.type = AST_UNARY_OP; u->base.location = loc;
        u->operator = "~"; u->operand = parse_unary(p);
        return (ExprNode *)u;
    }
    return parse_postfix(p);
}

static ExprNode *parse_postfix(Parser *p) {
    ExprNode *expr = parse_primary(p);
    if (!expr) return NULL;

    for (;;) {
        if (match(p, TOKEN_LPAREN)) {
            SourceLocation loc = expr->base.location;
            ExprNode **args = NULL; size_t arg_cnt = 0;
            bool is_printf = false;
            if (expr->base.type == AST_IDENTIFIER) {
                IdentifierNode *id = (IdentifierNode *)expr;
                is_printf = (!strcmp(id->name, "printf") || !strcmp(id->name, "print"));
            }
            if (!check(p, TOKEN_RPAREN)) {
                args = ocl_realloc(args, (arg_cnt + 1) * sizeof(ExprNode *));
                args[arg_cnt++] = parse_expression(p);
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
            expr = ast_create_call(loc, expr, args, arg_cnt);
            continue;
        }

        if (match(p, TOKEN_LBRACKET)) {
            SourceLocation loc = prev_tok(p)->location;
            ExprNode *index = parse_expression(p);
            consume(p, TOKEN_RBRACKET, "Expected ']'");
            expr = ast_create_index_access(loc, expr, index);
            continue;
        }

        if (match(p, TOKEN_DOT) || match(p, TOKEN_QUESTION_DOT)) {
            SourceLocation loc = prev_tok(p)->location;
            bool is_optional = prev_tok(p)->type == TOKEN_QUESTION_DOT;
            Token field_tok = consume(p, TOKEN_IDENTIFIER, "Expected field name after '.'");
            expr = ast_create_field_access(loc, expr, dup_lexeme(&field_tok), is_optional);
            continue;
        }

        break;
    }

    if (match(p, TOKEN_PLUS_PLUS)) {
        SourceLocation loc = prev_tok(p)->location;
        if (!is_assignable_expr(expr)) {
            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
                      "Postfix '++' requires an assignable expression");
            ast_free((ASTNode *)expr);
            return NULL;
        }
        return make_inc_dec(loc, expr, "+");
    }
    if (match(p, TOKEN_MINUS_MINUS)) {
        SourceLocation loc = prev_tok(p)->location;
        if (!is_assignable_expr(expr)) {
            error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
                      "Postfix '--' requires an assignable expression");
            ast_free((ASTNode *)expr);
            return NULL;
        }
        return make_inc_dec(loc, expr, "-");
    }

    return expr;
}

static ExprNode *parse_primary(Parser *p) {
    Token *t = cur_tok(p); SourceLocation loc = t->location;
    if (match(p, TOKEN_TRUE))  return ast_create_literal(loc, value_bool(true));
    if (match(p, TOKEN_FALSE)) return ast_create_literal(loc, value_bool(false));
    if (match(p, TOKEN_NULL))  return ast_create_literal(loc, value_null());
    if (check(p, TOKEN_INT))   { int64_t v = t->value.int_value;   p->current++; return ast_create_literal(loc, value_int(v)); }
    if (check(p, TOKEN_FLOAT)) { double v = t->value.float_value; p->current++; return ast_create_literal(loc, value_float(v)); }
    if (check(p, TOKEN_STRING)) {
        if (t->raw_string_value && strstr(t->raw_string_value, "${")) {
            p->current++;
            return parse_interpolated_string(p, t);
        }
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
    if (is_anon_func_start(p)) {
        p->current++;
        consume(p, TOKEN_LPAREN, "Expected '(' after func");

        ParamNode **params = NULL;
        size_t param_count = 0;
        if (!check(p, TOKEN_RPAREN)) {
            do {
                Token pname_tok = consume(p, TOKEN_IDENTIFIER, "Expected parameter name");
                char *pname = dup_lexeme(&pname_tok);
                consume(p, TOKEN_COLON, "Expected ':' after parameter name");
                TypeNode *ptype = parse_type(p);
                params = ocl_realloc(params, (param_count + 1) * sizeof(ParamNode *));
                params[param_count++] = ast_create_param(pname, ptype, pname_tok.location);
            } while (match(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RPAREN, "Expected ')' after parameters");
        consume(p, TOKEN_COLON, "Expected ':' before anonymous function return type");
        TypeNode *return_type = parse_type(p);
        BlockNode *body = parse_block(p);
        return ast_create_func_expr(loc, return_type, params, param_count, body);
    }
    if (check(p, TOKEN_IDENTIFIER)) {
        char *name = dup_lexeme(t); p->current++;
        if (match(p, TOKEN_LBRACE)) {
            char **field_names = NULL; ExprNode **field_values = NULL; size_t field_count = 0;
            while (!check(p, TOKEN_RBRACE) && !at_end(p)) {
                Token field_tok = consume(p, TOKEN_IDENTIFIER, "Expected field name");
                consume(p, TOKEN_COLON, "Expected ':' after field name");
                field_names = ocl_realloc(field_names, (field_count + 1) * sizeof(char *));
                field_values = ocl_realloc(field_values, (field_count + 1) * sizeof(ExprNode *));
                field_names[field_count] = dup_lexeme(&field_tok);
                field_values[field_count] = parse_expression(p);
                field_count++;
                if (!match(p, TOKEN_COMMA)) break;
            }
            consume(p, TOKEN_RBRACE, "Expected '}' after struct literal");
            return ast_create_struct_literal(loc, name, field_names, field_values, field_count);
        }
        return ast_create_identifier(loc, name);
    }
    if (match(p, TOKEN_LPAREN)) {
        ExprNode *e = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')'");
        return e;
    }
    if (match(p, TOKEN_LBRACKET)) {
        ExprNode **elems = NULL; size_t count = 0;
        while (!check(p, TOKEN_RBRACKET) && !at_end(p)) {
            elems = ocl_realloc(elems, (count + 1) * sizeof(ExprNode *));
            elems[count++] = parse_expression(p);
            if (!match(p, TOKEN_COMMA)) break;
        }
        consume(p, TOKEN_RBRACKET, "Expected ']' after array literal");
        return ast_create_array_literal(loc, elems, count);
    }

    error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
              "Unexpected token '%s' in expression", t->lexeme ? t->lexeme : "?");
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

/* Returns true if the current position looks like a C-style variable declaration. */
static bool is_c_style_var_decl(Parser *p) {
    return looks_like_typed_name(p);
}

/*
 * parse_for_init_var — parse the C-style typed variable declaration inside a
 * for-loop init clause: "Type name = expr"
 *
 * Unlike a regular C-style var decl, the initializer is REQUIRED here so that
 * the loop variable is always defined before the condition is checked.
 * Emits a clear error if "= expr" is missing.
 *
 * Returns an AST_VAR_DECL node.
 */
static ASTNode *parse_for_init_var(Parser *p, SourceLocation loc) {
    TypeNode *vtype = parse_type(p);
    Token n_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name in for-loop initializer");
    char *vname = dup_lexeme(&n_tok);

    ExprNode *vinit = NULL;
    if (match(p, TOKEN_EQUAL)) {
        vinit = parse_expression(p);
    } else {
        /* Initializer is required for for-loop variables. */
        error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, n_tok.location,
                  "for-loop variable '%s' must be initialized (e.g. %s %s = 0)",
                  vname,
                  vtype && vtype->type == TYPE_INT    ? "int"   :
                  vtype && vtype->type == TYPE_FLOAT  ? "float" :
                  vtype && vtype->type == TYPE_STRING ? "string" : "int",
                  vname);
        /* Supply a null initializer so codegen can continue. */
        vinit = ast_create_literal(loc, value_null());
    }

    return ast_create_var_decl(loc, vname, vtype, vinit);
}

/*
 * parse_for_init_let — parse the Let-style variable declaration inside a
 * for-loop init clause: "Let name:Type = expr"
 *
 * Initializer is REQUIRED.
 */
static ASTNode *parse_for_init_let(Parser *p, SourceLocation loc) {
    Token n_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
    char *vname = dup_lexeme(&n_tok);
    consume(p, TOKEN_COLON, "Expected ':' after variable name in for-loop initializer");
    TypeNode *vtype = parse_type(p);

    ExprNode *vinit = NULL;
    if (match(p, TOKEN_EQUAL)) {
        vinit = parse_expression(p);
    } else {
        error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, n_tok.location,
                  "for-loop variable '%s' must be initialized (e.g. Let %s:%s = 0)",
                  vname, vname,
                  vtype && vtype->type == TYPE_INT    ? "Int"    :
                  vtype && vtype->type == TYPE_FLOAT  ? "Float"  :
                  vtype && vtype->type == TYPE_STRING ? "String" : "Int");
        vinit = ast_create_literal(loc, value_null());
    }

    return ast_create_var_decl(loc, vname, vtype, vinit);
}

/*
 * Parse the else / else-if chain that follows a then-block.
 * Returns: NULL (no else), AST_IF_STMT (else if), or AST_BLOCK (plain else).
 */
static ASTNode *parse_else_chain(Parser *p) {
    if (!match(p, TOKEN_ELSE)) return NULL;
    if (check(p, TOKEN_IF)) return parse_statement(p); /* else if → recurse */
    return (ASTNode *)parse_block(p);                  /* plain else */
}

static void skip_brace_block(Parser *p) {
    int depth = 0;

    if (!check(p, TOKEN_LBRACE))
        return;

    do {
        if (check(p, TOKEN_LBRACE)) depth++;
        else if (check(p, TOKEN_RBRACE)) depth--;
        p->current++;
    } while (depth > 0 && !at_end(p));
}

static void skip_unsupported_function_decl(Parser *p) {
    if (!match(p, TOKEN_FUNC))
        return;

    while (!at_end(p)) {
        if (check(p, TOKEN_LBRACE)) {
            skip_brace_block(p);
            break;
        }
        if (check(p, TOKEN_SEMICOLON)) {
            p->current++;
            break;
        }
        p->current++;
    }
}

static ASTNode *parse_statement(Parser *p) {
    Token *t = cur_tok(p); SourceLocation loc = t->location;

    if (check(p, TOKEN_FUNC) && !is_anon_func_start(p)) {
        error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
                  "Nested functions are not supported. Move this declaration to top level");
        skip_unsupported_function_decl(p);
        return NULL;
    }
    if (check(p, TOKEN_EXTERN)) {
        error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, loc,
                  "Extern functions are only supported at top level");
        skip_unsupported_extern_decl(p);
        return NULL;
    }

    /* Import <name.ext> */
    if (match(p, TOKEN_IMPORT)) {
        consume(p, TOKEN_LESS, "Expected '<' after Import");
        char *name = parse_import_name(p);
        consume(p, TOKEN_GREATER, "Expected '>'");
        match(p, TOKEN_SEMICOLON);
        ImportNode *n = ocl_malloc(sizeof(ImportNode));
        n->base.type = AST_IMPORT; n->base.location = loc;
        n->filename = name;
        return (ASTNode *)n;
    }

    if (match(p, TOKEN_STRUCT)) {
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected struct name");
        char *name = dup_lexeme(&name_tok);
        ParamNode **fields = NULL; size_t field_count = 0;

        consume(p, TOKEN_LBRACE, "Expected '{' after struct name");
        while (!check(p, TOKEN_RBRACE) && !at_end(p)) {
            Token field_tok = consume(p, TOKEN_IDENTIFIER, "Expected field name");
            char *field_name = dup_lexeme(&field_tok);
            consume(p, TOKEN_COLON, "Expected ':' after field name");
            TypeNode *field_type = parse_type(p);
            fields = ocl_realloc(fields, (field_count + 1) * sizeof(ParamNode *));
            fields[field_count++] = ast_create_param(field_name, field_type, field_tok.location);
            consume_semicolon(p, "Expected ';' after struct field declaration");
        }
        consume(p, TOKEN_RBRACE, "Expected '}' after struct declaration");
        match(p, TOKEN_SEMICOLON);
        return ast_create_struct_decl(loc, name, fields, field_count);
    }

    /* declare name : Type */
    if (match(p, TOKEN_DECLARE)) {
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected identifier after 'declare'");
        char *name = dup_lexeme(&name_tok);
        TypeNode *type = match(p, TOKEN_COLON)
                         ? parse_type(p)
                         : ast_create_type(TYPE_UNKNOWN);
        consume_semicolon(p, "Expected ';' after declare statement");
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
        TypeNode *type = parse_type(p);
        ExprNode *init = match(p, TOKEN_EQUAL) ? parse_expression(p) : NULL;
        consume_semicolon(p, "Expected ';' after variable declaration");
        return ast_create_var_decl(loc, name, type, init);
    }

    /* C-style: Type name = expr */
    if (is_c_style_var_decl(p)) {
        TypeNode *type = parse_type(p);
        Token name_tok = consume(p, TOKEN_IDENTIFIER, "Expected variable name");
        char *name = dup_lexeme(&name_tok);
        ExprNode *init = match(p, TOKEN_EQUAL) ? parse_expression(p) : NULL;
        consume_semicolon(p, "Expected ';' after variable declaration");
        return ast_create_var_decl(loc, name, type, init);
    }

    /* if / else-if / else */
    if (match(p, TOKEN_IF)) {
        consume(p, TOKEN_LPAREN, "Expected '(' after if");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')' after if condition");
        BlockNode *then_b    = parse_block(p);
        ASTNode   *else_next = parse_else_chain(p);
        return ast_create_if_stmt(loc, cond, then_b, else_next);
    }

    /* while */
    if (match(p, TOKEN_WHILE)) {
        consume(p, TOKEN_LPAREN, "Expected '('");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')'");
        BlockNode *body = parse_block(p);
        LoopNode *lp = ocl_malloc(sizeof(LoopNode));
        lp->base.type = AST_WHILE_LOOP; lp->base.location = loc;
        lp->is_for = false; lp->init = NULL;
        lp->is_do_while = false;
        lp->condition = cond; lp->increment = NULL; lp->body = body;
        return (ASTNode *)lp;
    }

    if (match(p, TOKEN_DO)) {
        BlockNode *body = parse_block(p);
        consume(p, TOKEN_WHILE, "Expected 'while' after do block");
        consume(p, TOKEN_LPAREN, "Expected '(' after while");
        ExprNode *cond = parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')' after do while condition");
        consume_semicolon(p, "Expected ';' after do while statement");
        LoopNode *lp = ocl_malloc(sizeof(LoopNode));
        lp->base.type = AST_DO_WHILE_LOOP;
        lp->base.location = loc;
        lp->is_for = false;
        lp->is_do_while = true;
        lp->init = NULL;
        lp->condition = cond;
        lp->increment = NULL;
        lp->body = body;
        return (ASTNode *)lp;
    }

    /*
     * for loop
     *
     * Supported init forms:
     *   for (Let i:Int = 0; ...)      — OCL-style, initializer required
     *   for (int i = 0; ...)          — C-style, initializer required
     *   for (expr; ...)               — expression init (e.g. i = 0)
     *   for (; ...)                   — empty init
     *
     * The variable declared in the init clause is scoped to the loop
     * (visible in condition, body, and increment).
     */
    if (match(p, TOKEN_FOR)) {
        consume(p, TOKEN_LPAREN, "Expected '(' after 'for'");
        ASTNode *init = NULL;

        if (match(p, TOKEN_LET)) {
            /* Let i:Type = expr */
            init = parse_for_init_let(p, loc);
        } else if (is_c_style_var_decl(p)) {
            /* Type i = expr  — e.g. "int i = 0" */
            init = parse_for_init_var(p, loc);
        } else if (!check(p, TOKEN_SEMICOLON)) {
            /* Plain expression init — e.g. "i = 0" */
            init = (ASTNode *)parse_expression(p);
        }
        /* else: empty init clause, init stays NULL */

        consume_semicolon(p, "Expected ';' after for-loop initializer");

        ExprNode *cond = check(p, TOKEN_SEMICOLON) ? NULL : parse_expression(p);
        consume_semicolon(p, "Expected ';' after for-loop condition");

        ASTNode  *incr = check(p, TOKEN_RPAREN) ? NULL : (ASTNode *)parse_expression(p);
        consume(p, TOKEN_RPAREN, "Expected ')' after for-loop increment");

        BlockNode *body = parse_block(p);
        LoopNode *lp = ocl_malloc(sizeof(LoopNode));
        lp->base.type = AST_FOR_LOOP; lp->base.location = loc;
        lp->is_for = true; lp->init = init;
        lp->is_do_while = false;
        lp->condition = cond; lp->increment = incr; lp->body = body;
        return (ASTNode *)lp;
    }

    if (match(p, TOKEN_RETURN)) {
        ExprNode *val = (!check(p, TOKEN_SEMICOLON) && !check(p, TOKEN_RBRACE) && !at_end(p))
                        ? parse_expression(p) : NULL;
        consume_semicolon(p, "Expected ';' after return statement");
        return ast_create_return(loc, val);
    }
    if (match(p, TOKEN_BREAK)) {
        consume_semicolon(p, "Expected ';' after break statement");
        ASTNode *n = ocl_malloc(sizeof(ASTNode));
        n->type = AST_BREAK; n->location = loc;
        return n;
    }
    if (match(p, TOKEN_CONTINUE)) {
        consume_semicolon(p, "Expected ';' after continue statement");
        ASTNode *n = ocl_malloc(sizeof(ASTNode));
        n->type = AST_CONTINUE; n->location = loc;
        return n;
    }

    ExprNode *expr = parse_expression(p);
    if (!expr) { p->current++; return NULL; }
    consume_semicolon(p, "Expected ';' after expression statement");
    return (ASTNode *)expr;
}

Parser *parser_create(Token *tokens, size_t token_count,
                       const char *filename, ErrorCollector *errors) {
    Parser *p = ocl_malloc(sizeof(Parser));
    p->tokens = tokens; p->token_count = token_count; p->current = 0;
    p->filename = filename; p->errors = errors;
    p->state = parse_state_create();
    p->owns_state = true;
    return p;
}

void parser_free(Parser *p) {
    if (!p) return;
    if (p->owns_state) parse_state_free(p->state);
    ocl_free(p);
}

/* ── Import file resolution ─────────────────────────────────────────
   Search order:
     1. Same directory as the importing file
     2. Runtime roots from OCL_PROJECT_ROOT or the interpreter location
     3. Current working directory paths
   Extensions tried in order: as-is, .ocl
─────────────────────────────────────────────────────────────────── */
static char *find_import_file(const char *importing_file, const char *import_name) {
    char bases[OCL_IMPORT_BASE_MAX][OCL_IMPORT_PATH_MAX];
    char runtime_roots[OCL_IMPORT_ROOT_MAX][OCL_IMPORT_PATH_MAX];
    int  nb = 0;

    if (importing_file) {
        char importing_dir[OCL_IMPORT_PATH_MAX];

        if (copy_path(importing_dir, sizeof(importing_dir), importing_file) &&
            dirname_in_place(importing_dir)) {
            append_import_base(bases, &nb, importing_dir, import_name);
        } else {
            append_import_base(bases, &nb, NULL, import_name);
        }
    }

    {
        int root_count = resolve_runtime_roots(runtime_roots);
        for (int i = 0; i < root_count; i++) {
            append_import_subpath(bases, &nb, runtime_roots[i], "ocl_headers", import_name);
            append_import_subpath(bases, &nb, runtime_roots[i], "stdlib_headers", import_name);
            append_import_subpath(bases, &nb, runtime_roots[i], NULL, import_name);
        }
    }

    append_import_base(bases, &nb, "ocl_headers", import_name);
    append_import_base(bases, &nb, "stdlib_headers", import_name);
    append_import_base(bases, &nb, NULL, import_name);

    static const char *exts[] = { "", ".ocl" };
    static const int   next   = sizeof(exts) / sizeof(exts[0]);

    for (int i = 0; i < nb; i++) {
        for (int e = 0; e < next; e++) {
            char candidate[OCL_IMPORT_PATH_MAX + 8];
            size_t need = strlen(bases[i]) + strlen(exts[e]) + 1;
            if (need > sizeof(candidate))
                continue;
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
    char  *buf = ocl_malloc((size_t)sz + 1);
    size_t rd  = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static Parser *parser_create_child(Parser *parent, Token *tokens, size_t token_count,
                                   const char *filename) {
    Parser *child = ocl_malloc(sizeof(Parser));
    child->tokens = tokens;
    child->token_count = token_count;
    child->current = 0;
    child->filename = filename;
    child->errors = parent->errors;
    child->state = parent->state;
    child->owns_state = false;
    return child;
}

static void program_add_node(ProgramNode *prog, ASTNode *node) {
    if (!prog || !node) return;
    prog->nodes = ocl_realloc(prog->nodes, (prog->node_count + 1) * sizeof(ASTNode *));
    prog->nodes[prog->node_count++] = node;
}

static void program_add_import(ProgramNode *prog, ProgramNode *imported) {
    if (!prog || !imported) return;
    prog->imports = ocl_realloc(prog->imports, (prog->import_count + 1) * sizeof(ProgramNode *));
    prog->imports[prog->import_count++] = imported;
}

ProgramNode *parser_parse(Parser *p) {
    ProgramNode *prog = ocl_malloc(sizeof(ProgramNode));
    bool pushed_module = false;
    prog->base.type     = AST_PROGRAM;
    prog->base.location = (SourceLocation){1, 1, p->filename};
    prog->nodes     = NULL;
    prog->node_count = 0;
    prog->imports = NULL;
    prog->import_count = 0;
    prog->module_path = ocl_strdup(p->filename ? p->filename : "<memory>");

    if (p->filename && p->state && !parse_state_contains(p->state, p->filename)) {
        parse_state_push(p->state, p->filename);
        pushed_module = true;
    }

    while (!at_end(p)) {

        /* Function declaration */
        if ((check(p, TOKEN_FUNC) && !is_anon_func_start(p)) || check(p, TOKEN_EXTERN)) {
            ASTNode *fn = parse_top_level_function_decl(p);
            program_add_node(prog, fn);
            continue;
        }

        /* Import — resolve and inline the file */
        if (check(p, TOKEN_IMPORT)) {
            ASTNode *import_node = parse_statement(p);
            if (!import_node) continue;
            ImportNode *imp = (ImportNode *)import_node;

            char *resolved = find_import_file(p->filename, imp->filename);
            if (resolved) {
                if (parse_state_contains(p->state, resolved)) {
                    error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, import_node->location,
                              "Circular import detected: '%s'", imp->filename);
                } else {
                    char *src = read_file_import(resolved);
                    if (src) {
                        Lexer       *sub_lex    = lexer_create(src, resolved);
                        size_t       sub_count  = 0;
                        Token       *sub_tokens = lexer_tokenize_all(sub_lex, &sub_count);
                        lexer_free(sub_lex);

                        Parser      *sub      = parser_create_child(p, sub_tokens, sub_count, resolved);
                        ProgramNode *sub_prog = parser_parse(sub);
                        parser_free(sub);
                        tokens_free(sub_tokens, sub_count);
                        ocl_free(src);

                        if (sub_prog)
                            program_add_import(prog, sub_prog);
                    }
                }
                ocl_free(resolved);
            } else {
                error_add(p->errors, ERRK_SYNTAX, ERROR_PARSER, import_node->location,
                          "Import not found: '%s'", imp->filename);
            }

            program_add_node(prog, import_node);
            continue;
        }

        ASTNode *stmt = parse_statement(p);
        if (stmt) program_add_node(prog, stmt);
    }
    if (pushed_module)
        parse_state_pop(p->state);
    return prog;
}


