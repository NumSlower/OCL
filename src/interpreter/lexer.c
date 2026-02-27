#include <stdlib.h>    /* strtod, strtoll — MUST be first to avoid implicit declaration */
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "common.h"

/* ── Helpers ──────────────────────────────────────────────────── */
static inline char cur(const Lexer *l)  { return l->source[l->position]; }
static inline char peek(const Lexer *l) {
    size_t nxt = l->position + 1;
    return (nxt < l->source_len) ? l->source[nxt] : '\0';
}

static void advance(Lexer *l) {
    if (l->position >= l->source_len) return;
    if (l->source[l->position] == '\n') {
        l->line++;
        l->column = 1;
    } else {
        l->column++;
    }
    l->position++;
}

static void skip_whitespace(Lexer *l) {
    while (l->position < l->source_len) {
        char c = cur(l);
        if (c == ' ' || c == '\t' || c == '\r')
            advance(l);
        else if (c == '/' && peek(l) == '#') {
            /* block comment /# ... #/ */
            advance(l); advance(l); /* skip /# */
            while (l->position < l->source_len) {
                if (cur(l) == '#' && peek(l) == '/') {
                    advance(l); advance(l); /* skip #/ */
                    break;
                }
                advance(l);
            }
        } else {
            break;
        }
    }
}

static Token make_token(Lexer *l, TokenType t, const char *lex) {
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type     = t;
    tok.lexeme   = ocl_strdup(lex);
    tok.lexeme_length = strlen(lex);
    tok.location = (SourceLocation){l->line, l->column, l->filename};
    return tok;
}

/* ── String / char literal ────────────────────────────────────── */
static Token read_string(Lexer *l, TokenType t) {
    int start_line = l->line, start_col = l->column;
    char quote = cur(l);
    advance(l); /* skip opening quote */

    char buf[4096]; size_t len = 0;
    while (l->position < l->source_len && cur(l) != quote) {
        char c = cur(l);
        if (c == '\\') {
            advance(l);
            switch (cur(l)) {
                case 'n':  buf[len++] = '\n'; break;
                case 't':  buf[len++] = '\t'; break;
                case 'r':  buf[len++] = '\r'; break;
                case '\\': buf[len++] = '\\'; break;
                case '"':  buf[len++] = '"';  break;
                case '\'': buf[len++] = '\''; break;
                case '0':  buf[len++] = '\0'; break;
                default:   buf[len++] = cur(l); break;
            }
        } else {
            buf[len++] = c;
        }
        advance(l);
        if (len >= sizeof(buf) - 1) break;
    }
    buf[len] = '\0';
    if (l->position < l->source_len) advance(l); /* skip closing quote */

    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type               = t;
    tok.location           = (SourceLocation){start_line, start_col, l->filename};
    tok.value.string_value = ocl_strdup(buf);
    tok.value.string_length = len;
    tok.lexeme             = ocl_strdup(buf);
    tok.lexeme_length      = len;
    return tok;
}

/* ── Number literal ───────────────────────────────────────────── */
static Token read_number(Lexer *l) {
    int start_line = l->line, start_col = l->column;
    char buf[64]; size_t len = 0;
    bool is_float = false;

    while (l->position < l->source_len && isdigit((unsigned char)cur(l)))
        { buf[len++] = cur(l); advance(l); }

    if (l->position < l->source_len && cur(l) == '.' && isdigit((unsigned char)peek(l))) {
        is_float = true;
        buf[len++] = '.'; advance(l);
        while (l->position < l->source_len && isdigit((unsigned char)cur(l)))
            { buf[len++] = cur(l); advance(l); }
    }
    buf[len] = '\0';

    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.location      = (SourceLocation){start_line, start_col, l->filename};
    tok.lexeme        = ocl_strdup(buf);
    tok.lexeme_length = len;
    if (is_float) {
        tok.type              = TOKEN_FLOAT;
        tok.value.float_value = strtod(buf, NULL);   /* strtod declared via <stdlib.h> above */
    } else {
        tok.type            = TOKEN_INT;
        tok.value.int_value = (int64_t)strtoll(buf, NULL, 10);
    }
    return tok;
}

/* ── Identifier / keyword ─────────────────────────────────────── */
static Token read_identifier(Lexer *l) {
    int start_line = l->line, start_col = l->column;
    char buf[256]; size_t len = 0;

    while (l->position < l->source_len) {
        char c = cur(l);
        if (isalnum((unsigned char)c) || c == '_')
            { buf[len++] = c; advance(l); }
        else break;
    }
    buf[len] = '\0';

    TokenType t = TOKEN_IDENTIFIER;
    if      (!strcmp(buf, "Let"))      t = TOKEN_LET;
    else if (!strcmp(buf, "func"))     t = TOKEN_FUNC;
    else if (!strcmp(buf, "return"))   t = TOKEN_RETURN;
    else if (!strcmp(buf, "if"))       t = TOKEN_IF;
    else if (!strcmp(buf, "else"))     t = TOKEN_ELSE;
    else if (!strcmp(buf, "for"))      t = TOKEN_FOR;
    else if (!strcmp(buf, "while"))    t = TOKEN_WHILE;
    else if (!strcmp(buf, "Import"))   t = TOKEN_IMPORT;
    else if (!strcmp(buf, "declare"))  t = TOKEN_DECLARE;
    else if (!strcmp(buf, "true"))     t = TOKEN_TRUE;
    else if (!strcmp(buf, "false"))    t = TOKEN_FALSE;
    else if (!strcmp(buf, "break"))    t = TOKEN_BREAK;
    else if (!strcmp(buf, "continue")) t = TOKEN_CONTINUE;

    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type     = t;
    tok.location = (SourceLocation){start_line, start_col, l->filename};
    tok.lexeme   = ocl_strdup(buf);
    tok.lexeme_length = len;
    if (t == TOKEN_TRUE)  tok.value.int_value = 1;
    if (t == TOKEN_FALSE) tok.value.int_value = 0;
    return tok;
}

/* ── Public API ───────────────────────────────────────────────── */
Lexer *lexer_create(const char *source, const char *filename) {
    Lexer *l    = ocl_malloc(sizeof(Lexer));
    l->source   = source;
    l->source_len = strlen(source);
    l->position = 0;
    l->line     = 1;
    l->column   = 1;
    l->filename = filename;
    return l;
}

void lexer_free(Lexer *l) { ocl_free(l); }

void token_free(Token *t) {
    if (!t) return;
    ocl_free(t->lexeme);
    if (t->type == TOKEN_STRING || t->type == TOKEN_CHAR)
        ocl_free(t->value.string_value);
}

void tokens_free(Token *tokens, size_t count) {
    if (!tokens) return;
    for (size_t i = 0; i < count; i++) token_free(&tokens[i]);
    ocl_free(tokens);
}

Token lexer_next_token(Lexer *l) {
    skip_whitespace(l);

    if (l->position >= l->source_len)
        return make_token(l, TOKEN_EOF, "");

    char c = cur(l);

    /* Newline */
    if (c == '\n') {
        Token t = make_token(l, TOKEN_NEWLINE, "\\n");
        advance(l);
        return t;
    }

    /* String literals */
    if (c == '"')  return read_string(l, TOKEN_STRING);
    if (c == '\'') return read_string(l, TOKEN_CHAR);

    /* Numbers */
    if (isdigit((unsigned char)c)) return read_number(l);

    /* Identifiers / keywords */
    if (isalpha((unsigned char)c) || c == '_') return read_identifier(l);

    /* ── Single / double-char operators ── */
    int start_line = l->line, start_col = l->column;
    advance(l); /* consume first char */

    switch (c) {
        case '+': return make_token(l, TOKEN_PLUS,      "+");
        case '-':
            if (cur(l) == '>') { advance(l); return make_token(l, TOKEN_ARROW, "->"); }
            return make_token(l, TOKEN_MINUS, "-");
        case '*': return make_token(l, TOKEN_STAR,      "*");
        case '/': return make_token(l, TOKEN_SLASH,     "/");
        case '%': return make_token(l, TOKEN_PERCENT,   "%");
        case ':': return make_token(l, TOKEN_COLON,     ":");
        case ';': return make_token(l, TOKEN_SEMICOLON, ";");
        case '.': return make_token(l, TOKEN_DOT,       ".");
        case ',': return make_token(l, TOKEN_COMMA,     ",");
        case '(': return make_token(l, TOKEN_LPAREN,    "(");
        case ')': return make_token(l, TOKEN_RPAREN,    ")");
        case '{': return make_token(l, TOKEN_LBRACE,    "{");
        case '}': return make_token(l, TOKEN_RBRACE,    "}");
        case '[': return make_token(l, TOKEN_LBRACKET,  "[");
        case ']': return make_token(l, TOKEN_RBRACKET,  "]");
        case '=':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_EQUAL_EQUAL, "=="); }
            return make_token(l, TOKEN_EQUAL, "=");
        case '!':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_BANG_EQUAL, "!="); }
            return make_token(l, TOKEN_BANG, "!");
        case '<':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_LESS_EQUAL, "<="); }
            return make_token(l, TOKEN_LESS, "<");
        case '>':
            if (cur(l) == '=') { advance(l); return make_token(l, TOKEN_GREATER_EQUAL, ">="); }
            return make_token(l, TOKEN_GREATER, ">");
        case '&':
            if (cur(l) == '&') { advance(l); return make_token(l, TOKEN_AMPERSAND_AMPERSAND, "&&"); }
            break;
        case '|':
            if (cur(l) == '|') { advance(l); return make_token(l, TOKEN_PIPE_PIPE, "||"); }
            break;
        default: break;
    }

    /* Unknown character */
    char err[4] = {c, '\0'};
    Token t = make_token(l, TOKEN_ERROR, err);
    t.location = (SourceLocation){start_line, start_col, l->filename};
    return t;
}

Token *lexer_tokenize_all(Lexer *l, size_t *out_count) {
    size_t  cap    = 256;
    size_t  count  = 0;
    Token  *tokens = ocl_malloc(cap * sizeof(Token));

    for (;;) {
        if (count >= cap) {
            cap   *= 2;
            tokens = ocl_realloc(tokens, cap * sizeof(Token));
        }
        Token t = lexer_next_token(l);
        tokens[count++] = t;
        if (t.type == TOKEN_EOF) break;
    }
    *out_count = count;
    return tokens;
}