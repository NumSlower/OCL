#ifndef OCL_LEXER_H
#define OCL_LEXER_H

#include "common.h"

typedef enum {
    TOKEN_EOF,
    TOKEN_LET,
    TOKEN_FUNC,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_DO,
    TOKEN_WHILE,
    TOKEN_IMPORT,
    TOKEN_DECLARE,
    TOKEN_STRUCT,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_IDENTIFIER,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_CHAR,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_PLUS_PLUS,
    TOKEN_MINUS_MINUS,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS_EQUAL,
    TOKEN_STAR_EQUAL,
    TOKEN_SLASH_EQUAL,
    TOKEN_PERCENT_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,
    TOKEN_SHIFT_LEFT_EQUAL,
    TOKEN_SHIFT_RIGHT_EQUAL,
    TOKEN_AMPERSAND,
    TOKEN_PIPE,
    TOKEN_CARET,
    TOKEN_TILDE,
    TOKEN_AMPERSAND_AMPERSAND,
    TOKEN_PIPE_PIPE,
    TOKEN_AMPERSAND_EQUAL,
    TOKEN_PIPE_EQUAL,
    TOKEN_CARET_EQUAL,
    TOKEN_BANG,
    TOKEN_QUESTION,
    TOKEN_QUESTION_QUESTION,
    TOKEN_QUESTION_DOT,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_DOT,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_ARROW,
    TOKEN_NEWLINE,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType      type;
    char          *lexeme;
    size_t         lexeme_length;
    SourceLocation location;
    char          *raw_string_value;
    size_t         raw_string_length;
    union {
        int64_t int_value;
        double  float_value;
        struct {
            char  *string_value;
            size_t string_length;
        };
    } value;
} Token;

typedef struct Lexer {
    const char *source;
    size_t      source_len;
    size_t      position;
    int         line;
    int         column;
    const char *filename;
} Lexer;

Lexer  *lexer_create(const char *source, const char *filename);
void    lexer_free(Lexer *lexer);
Token   lexer_next_token(Lexer *lexer);
Token  *lexer_tokenize_all(Lexer *lexer, size_t *token_count);
void    token_free(Token *t);
void    tokens_free(Token *tokens, size_t count);

#endif
