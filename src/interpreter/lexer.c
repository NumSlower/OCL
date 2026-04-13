#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} LexerBuffer;

static inline char cur(const Lexer *l) {
    return l->source[l->position];
}

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
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(l);
        } else if (c == '/' && peek(l) == '#') {
            advance(l);
            advance(l);
            while (l->position < l->source_len) {
                if (cur(l) == '#' && peek(l) == '/') {
                    advance(l);
                    advance(l);
                    break;
                }
                advance(l);
            }
        } else {
            break;
        }
    }
}

static void lexer_buffer_append(LexerBuffer *buffer, char ch) {
    if (!buffer) return;
    if (buffer->len + 1 >= buffer->cap) {
        size_t new_cap = buffer->cap ? buffer->cap * 2 : 32;
        while (new_cap <= buffer->len + 1)
            new_cap *= 2;
        buffer->data = ocl_realloc(buffer->data, new_cap);
        buffer->cap = new_cap;
    }
    buffer->data[buffer->len++] = ch;
    buffer->data[buffer->len] = '\0';
}

static char *lexer_buffer_take(LexerBuffer *buffer) {
    char *result;

    if (!buffer) return ocl_strdup("");
    if (!buffer->data)
        return ocl_strdup("");

    result = buffer->data;
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
    return result;
}

static void lexer_buffer_free(LexerBuffer *buffer) {
    if (!buffer) return;
    ocl_free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

static Token make_token(Lexer *l, TokenType t, const char *lex) {
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type = t;
    tok.lexeme = ocl_strdup(lex);
    tok.lexeme_length = strlen(lex);
    tok.location = (SourceLocation){l->line, l->column, l->filename};
    return tok;
}

static Token make_token_at(Lexer *l, TokenType t, const char *lex, int line, int column) {
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type = t;
    tok.lexeme = ocl_strdup(lex);
    tok.lexeme_length = strlen(lex);
    tok.location = (SourceLocation){line, column, l->filename};
    return tok;
}

static Token read_string(Lexer *l, TokenType t) {
    int start_line = l->line;
    int start_col = l->column;
    char quote = cur(l);
    LexerBuffer value_buf = {0};
    LexerBuffer raw_buf = {0};

    advance(l);
    while (l->position < l->source_len && cur(l) != quote) {
        char c = cur(l);

        /* Unterminated string: stop at newline or EOF */
        if (c == '\n' || l->position >= l->source_len) {
            Token tok;
            memset(&tok, 0, sizeof(tok));
            tok.type = TOKEN_ERROR;
            tok.location = (SourceLocation){start_line, start_col, l->filename};
            tok.lexeme = ocl_strdup("unterminated string literal");
            tok.lexeme_length = strlen(tok.lexeme);
            lexer_buffer_free(&value_buf);
            lexer_buffer_free(&raw_buf);
            return tok;
        }

        if (c == '\\') {
            lexer_buffer_append(&raw_buf, c);
            advance(l);
            if (l->position >= l->source_len)
                break;
            lexer_buffer_append(&raw_buf, cur(l));
            switch (cur(l)) {
                case 'n':  lexer_buffer_append(&value_buf, '\n'); break;
                case 't':  lexer_buffer_append(&value_buf, '\t'); break;
                case 'r':  lexer_buffer_append(&value_buf, '\r'); break;
                case '\\': lexer_buffer_append(&value_buf, '\\'); break;
                case '"':  lexer_buffer_append(&value_buf, '"');  break;
                case '\'': lexer_buffer_append(&value_buf, '\''); break;
                case '0':  lexer_buffer_append(&value_buf, '\0'); break;
                default:   lexer_buffer_append(&value_buf, cur(l)); break;
            }
        } else {
            lexer_buffer_append(&raw_buf, c);
            lexer_buffer_append(&value_buf, c);
        }
        advance(l);
    }
    if (l->position < l->source_len) advance(l);

    Token tok;
    size_t value_len = value_buf.len;
    size_t raw_len = raw_buf.len;
    memset(&tok, 0, sizeof(tok));
    tok.type = t;
    tok.location = (SourceLocation){start_line, start_col, l->filename};
    tok.value.string_value = lexer_buffer_take(&value_buf);
    tok.value.string_length = value_len;
    tok.lexeme = ocl_strdup(tok.value.string_value);
    tok.lexeme_length = value_len;
    tok.raw_string_value = lexer_buffer_take(&raw_buf);
    tok.raw_string_length = raw_len;
    return tok;
}

static Token read_number(Lexer *l) {
    int start_line = l->line;
    int start_col = l->column;
    LexerBuffer buf = {0};
    bool is_float = false;

    while (l->position < l->source_len && isdigit((unsigned char)cur(l))) {
        lexer_buffer_append(&buf, cur(l));
        advance(l);
    }
    if (l->position < l->source_len && cur(l) == '.' && isdigit((unsigned char)peek(l))) {
        is_float = true;
        lexer_buffer_append(&buf, '.');
        advance(l);
        while (l->position < l->source_len && isdigit((unsigned char)cur(l))) {
            lexer_buffer_append(&buf, cur(l));
            advance(l);
        }
    }

    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.location = (SourceLocation){start_line, start_col, l->filename};
    tok.lexeme = lexer_buffer_take(&buf);
    tok.lexeme_length = strlen(tok.lexeme);
    if (is_float) {
        tok.type = TOKEN_FLOAT;
        tok.value.float_value = strtod(tok.lexeme, NULL);
    } else {
        tok.type = TOKEN_INT;
        tok.value.int_value = (int64_t)strtoll(tok.lexeme, NULL, 10);
    }
    return tok;
}

static Token read_identifier(Lexer *l) {
    int start_line = l->line;
    int start_col = l->column;
    LexerBuffer buf = {0};

    while (l->position < l->source_len) {
        char c = cur(l);
        if (isalnum((unsigned char)c) || c == '_') {
            lexer_buffer_append(&buf, c);
            advance(l);
        } else {
            break;
        }
    }

    char *identifier = lexer_buffer_take(&buf);
    TokenType t = TOKEN_IDENTIFIER;
    if (!strcmp(identifier, "Let")) t = TOKEN_LET;
    else if (!strcmp(identifier, "func")) t = TOKEN_FUNC;
    else if (!strcmp(identifier, "return")) t = TOKEN_RETURN;
    else if (!strcmp(identifier, "if")) t = TOKEN_IF;
    else if (!strcmp(identifier, "else")) t = TOKEN_ELSE;
    else if (!strcmp(identifier, "for")) t = TOKEN_FOR;
    else if (!strcmp(identifier, "do")) t = TOKEN_DO;
    else if (!strcmp(identifier, "while")) t = TOKEN_WHILE;
    else if (!strcmp(identifier, "Import")) t = TOKEN_IMPORT;
    else if (!strcmp(identifier, "declare")) t = TOKEN_DECLARE;
    else if (!strcmp(identifier, "struct")) t = TOKEN_STRUCT;
    else if (!strcmp(identifier, "true")) t = TOKEN_TRUE;
    else if (!strcmp(identifier, "false")) t = TOKEN_FALSE;
    else if (!strcmp(identifier, "null")) t = TOKEN_NULL;
    else if (!strcmp(identifier, "break")) t = TOKEN_BREAK;
    else if (!strcmp(identifier, "continue")) t = TOKEN_CONTINUE;

    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type = t;
    tok.location = (SourceLocation){start_line, start_col, l->filename};
    tok.lexeme = identifier;
    tok.lexeme_length = strlen(identifier);
    if (t == TOKEN_TRUE) tok.value.int_value = 1;
    if (t == TOKEN_FALSE) tok.value.int_value = 0;
    return tok;
}

Lexer *lexer_create(const char *source, const char *filename) {
    Lexer *l = ocl_malloc(sizeof(Lexer));
    l->source = source;
    l->source_len = strlen(source);
    l->position = 0;
    l->line = 1;
    l->column = 1;
    l->filename = filename;

    if (l->source_len >= 3 &&
        (unsigned char)l->source[0] == 0xEF &&
        (unsigned char)l->source[1] == 0xBB &&
        (unsigned char)l->source[2] == 0xBF) {
        l->position = 3;
        l->column = 1;
    }
    return l;
}

void lexer_free(Lexer *l) {
    ocl_free(l);
}

void token_free(Token *t) {
    if (!t) return;
    ocl_free(t->lexeme);
    ocl_free(t->raw_string_value);
    if (t->type == TOKEN_STRING || t->type == TOKEN_CHAR)
        ocl_free(t->value.string_value);
}

void tokens_free(Token *tokens, size_t count) {
    if (!tokens) return;
    for (size_t i = 0; i < count; i++)
        token_free(&tokens[i]);
    ocl_free(tokens);
}

Token lexer_next_token(Lexer *l) {
    skip_whitespace(l);
    if (l->position >= l->source_len) return make_token(l, TOKEN_EOF, "");

    char c = cur(l);
    if (c == '\n') {
        Token t = make_token(l, TOKEN_NEWLINE, "\\n");
        advance(l);
        return t;
    }
    if (c == '"') return read_string(l, TOKEN_STRING);
    if (c == '\'') return read_string(l, TOKEN_CHAR);
    if (isdigit((unsigned char)c)) return read_number(l);
    if (isalpha((unsigned char)c) || c == '_') return read_identifier(l);

    int start_line = l->line;
    int start_col = l->column;
    advance(l);

    switch (c) {
        case '+':
            if (cur(l) == '+') { advance(l); return make_token_at(l, TOKEN_PLUS_PLUS, "++", start_line, start_col); }
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_PLUS_EQUAL, "+=", start_line, start_col); }
            return make_token_at(l, TOKEN_PLUS, "+", start_line, start_col);
        case '-':
            if (cur(l) == '-') { advance(l); return make_token_at(l, TOKEN_MINUS_MINUS, "--", start_line, start_col); }
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_MINUS_EQUAL, "-=", start_line, start_col); }
            if (cur(l) == '>') { advance(l); return make_token_at(l, TOKEN_ARROW, "->", start_line, start_col); }
            return make_token_at(l, TOKEN_MINUS, "-", start_line, start_col);
        case '*':
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_STAR_EQUAL, "*=", start_line, start_col); }
            return make_token_at(l, TOKEN_STAR, "*", start_line, start_col);
        case '/':
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_SLASH_EQUAL, "/=", start_line, start_col); }
            return make_token_at(l, TOKEN_SLASH, "/", start_line, start_col);
        case '%':
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_PERCENT_EQUAL, "%=", start_line, start_col); }
            return make_token_at(l, TOKEN_PERCENT, "%", start_line, start_col);
        case '?':
            if (cur(l) == '?') { advance(l); return make_token_at(l, TOKEN_QUESTION_QUESTION, "??", start_line, start_col); }
            if (cur(l) == '.') { advance(l); return make_token_at(l, TOKEN_QUESTION_DOT, "?.", start_line, start_col); }
            return make_token_at(l, TOKEN_QUESTION, "?", start_line, start_col);
        case ':': return make_token_at(l, TOKEN_COLON, ":", start_line, start_col);
        case ';': return make_token_at(l, TOKEN_SEMICOLON, ";", start_line, start_col);
        case '.': return make_token_at(l, TOKEN_DOT, ".", start_line, start_col);
        case ',': return make_token_at(l, TOKEN_COMMA, ",", start_line, start_col);
        case '(': return make_token_at(l, TOKEN_LPAREN, "(", start_line, start_col);
        case ')': return make_token_at(l, TOKEN_RPAREN, ")", start_line, start_col);
        case '{': return make_token_at(l, TOKEN_LBRACE, "{", start_line, start_col);
        case '}': return make_token_at(l, TOKEN_RBRACE, "}", start_line, start_col);
        case '[': return make_token_at(l, TOKEN_LBRACKET, "[", start_line, start_col);
        case ']': return make_token_at(l, TOKEN_RBRACKET, "]", start_line, start_col);
        case '=':
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_EQUAL_EQUAL, "==", start_line, start_col); }
            return make_token_at(l, TOKEN_EQUAL, "=", start_line, start_col);
        case '!':
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_BANG_EQUAL, "!=", start_line, start_col); }
            return make_token_at(l, TOKEN_BANG, "!", start_line, start_col);
        case '<':
            if (cur(l) == '<') {
                advance(l);
                if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_SHIFT_LEFT_EQUAL, "<<=", start_line, start_col); }
                return make_token_at(l, TOKEN_SHIFT_LEFT, "<<", start_line, start_col);
            }
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_LESS_EQUAL, "<=", start_line, start_col); }
            return make_token_at(l, TOKEN_LESS, "<", start_line, start_col);
        case '>':
            if (cur(l) == '>') {
                advance(l);
                if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_SHIFT_RIGHT_EQUAL, ">>=", start_line, start_col); }
                return make_token_at(l, TOKEN_SHIFT_RIGHT, ">>", start_line, start_col);
            }
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_GREATER_EQUAL, ">=", start_line, start_col); }
            return make_token_at(l, TOKEN_GREATER, ">", start_line, start_col);
        case '&':
            if (cur(l) == '&') { advance(l); return make_token_at(l, TOKEN_AMPERSAND_AMPERSAND, "&&", start_line, start_col); }
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_AMPERSAND_EQUAL, "&=", start_line, start_col); }
            return make_token_at(l, TOKEN_AMPERSAND, "&", start_line, start_col);
        case '|':
            if (cur(l) == '|') { advance(l); return make_token_at(l, TOKEN_PIPE_PIPE, "||", start_line, start_col); }
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_PIPE_EQUAL, "|=", start_line, start_col); }
            return make_token_at(l, TOKEN_PIPE, "|", start_line, start_col);
        case '^':
            if (cur(l) == '=') { advance(l); return make_token_at(l, TOKEN_CARET_EQUAL, "^=", start_line, start_col); }
            return make_token_at(l, TOKEN_CARET, "^", start_line, start_col);
        case '~':
            return make_token_at(l, TOKEN_TILDE, "~", start_line, start_col);
        default:
            break;
    }

    char err[4] = {c, '\0'};
    return make_token_at(l, TOKEN_ERROR, err, start_line, start_col);
}

Token *lexer_tokenize_all(Lexer *l, size_t *out_count) {
    size_t cap = 256;
    size_t count = 0;
    Token *tokens = ocl_malloc(cap * sizeof(Token));

    for (;;) {
        if (count >= cap) {
            cap *= 2;
            tokens = ocl_realloc(tokens, cap * sizeof(Token));
        }
        Token t = lexer_next_token(l);
        tokens[count++] = t;
        if (t.type == TOKEN_EOF) break;
    }

    *out_count = count;
    return tokens;
}
