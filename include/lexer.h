#ifndef OCL_LEXER_H
#define OCL_LEXER_H

#include "common.h"

/* Token types */
typedef enum {
    /* End of file */
    TOKEN_EOF,
    
    /* Keywords */
    TOKEN_LET,
    TOKEN_FUNC,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_IMPORT,
    TOKEN_DECLARE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    
    /* Identifiers and literals */
    TOKEN_IDENTIFIER,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_CHAR,
    
    /* Operators */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_AMPERSAND_AMPERSAND,
    TOKEN_PIPE_PIPE,
    TOKEN_BANG,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_DOT,
    TOKEN_COMMA,
    TOKEN_ARROW,
    
    /* Delimiters */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_ANGLE_LEFT,
    TOKEN_ANGLE_RIGHT,
    
    /* Special */
    TOKEN_NEWLINE,
    TOKEN_ERROR
} TokenType;

/* Token structure */
typedef struct {
    TokenType type;
    const char *lexeme;
    size_t lexeme_length;
    SourceLocation location;
    
    /* For literals */
    union {
        int64_t int_value;
        double float_value;
        struct {
            char *string_value;
            size_t string_length;
        };
    } value;
} Token;

/* Lexer structure */
typedef struct Lexer {
    const char *source;
    size_t position;
    size_t read_position;
    char current_char;
    int line;
    int column;
    const char *filename;
} Lexer;

/* Lexer functions */
Lexer *lexer_create(const char *source, const char *filename);
void lexer_free(Lexer *lexer);
Token lexer_next_token(Lexer *lexer);
Token *lexer_tokenize_all(Lexer *lexer, size_t *token_count);

#endif /* OCL_LEXER_H */
