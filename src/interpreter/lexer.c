#include "lexer.h"
#include "common.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Helper functions */
static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_alphanumeric(char c) {
    return is_alpha(c) || is_digit(c);
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static void lexer_advance(Lexer *lexer) {
    if (lexer->current_char == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    
    lexer->position = lexer->read_position;
    if (lexer->position < strlen(lexer->source)) {
        lexer->current_char = lexer->source[lexer->position];
        lexer->read_position++;
    } else {
        lexer->current_char = '\0';
        lexer->read_position++;
    }
}

static char lexer_peek(Lexer *lexer) {
    if (lexer->read_position < strlen(lexer->source)) {
        return lexer->source[lexer->read_position];
    }
    return '\0';
}

static void lexer_skip_whitespace(Lexer *lexer) {
    while (is_whitespace(lexer->current_char)) {
        lexer_advance(lexer);
    }
}

static void lexer_skip_block_comment(Lexer *lexer) {
    /* Skip /# ... #/ */
    if (lexer->current_char == '/' && lexer_peek(lexer) == '#') {
        lexer_advance(lexer);  /* Skip / */
        lexer_advance(lexer);  /* Skip # */
        
        while (lexer->current_char != '\0') {
            if (lexer->current_char == '#' && lexer_peek(lexer) == '/') {
                lexer_advance(lexer);  /* Skip # */
                lexer_advance(lexer);  /* Skip / */
                return;
            }
            lexer_advance(lexer);
        }
    }
}

static Token lexer_read_string(Lexer *lexer) {
    Token token;
    memset(&token, 0, sizeof(Token));
    token.location = (SourceLocation){lexer->line, lexer->column, lexer->filename};
    token.type = TOKEN_STRING;
    
    char quote = lexer->current_char;
    lexer_advance(lexer);  /* Skip opening quote */
    
    char buffer[1024] = {0};
    size_t len = 0;
    
    while (lexer->current_char != quote && lexer->current_char != '\0') {
        if (lexer->current_char == '\\') {
            lexer_advance(lexer);
            switch (lexer->current_char) {
                case 'n': buffer[len++] = '\n'; break;
                case 't': buffer[len++] = '\t'; break;
                case 'r': buffer[len++] = '\r'; break;
                case '\\': buffer[len++] = '\\'; break;
                case '"': buffer[len++] = '"'; break;
                case '\'': buffer[len++] = '\''; break;
                default: buffer[len++] = lexer->current_char;
            }
        } else {
            buffer[len++] = lexer->current_char;
        }
        lexer_advance(lexer);
    }
    
    if (lexer->current_char == quote) {
        lexer_advance(lexer);  /* Skip closing quote */
    }
    
    token.value.string_value = (char *)ocl_malloc(len + 1);
    strcpy(token.value.string_value, buffer);
    token.value.string_length = len;
    token.lexeme_length = len;
    token.lexeme = token.value.string_value;  /* Point to the allocated string */
    
    return token;
}

static Token lexer_read_number(Lexer *lexer) {
    Token token;
    memset(&token, 0, sizeof(Token));
    token.location = (SourceLocation){lexer->line, lexer->column, lexer->filename};
    token.type = TOKEN_INT;
    
    char buffer[64] = {0};
    size_t len = 0;
    
    /* Read all digits before decimal point */
    while (is_digit(lexer->current_char)) {
        buffer[len++] = lexer->current_char;
        lexer_advance(lexer);
    }
    
    /* Check for decimal point */
    if (lexer->current_char == '.' && is_digit(lexer_peek(lexer))) {
        token.type = TOKEN_FLOAT;
        buffer[len++] = '.';
        lexer_advance(lexer);
        
        while (is_digit(lexer->current_char)) {
            buffer[len++] = lexer->current_char;
            lexer_advance(lexer);
        }
    }
    
    buffer[len] = '\0';
    
    if (token.type == TOKEN_INT) {
        token.value.int_value = strtoll(buffer, NULL, 10);
    } else {
        token.value.float_value = strtod(buffer, NULL);
    }
    
    token.lexeme_length = len;
    
    /* Allocate memory for lexeme */
    token.lexeme = (char *)ocl_malloc(len + 1);
    strcpy((char *)token.lexeme, buffer);
    
    return token;
}

static Token lexer_read_identifier(Lexer *lexer) {
    Token token;
    memset(&token, 0, sizeof(Token));
    token.location = (SourceLocation){lexer->line, lexer->column, lexer->filename};
    token.type = TOKEN_IDENTIFIER;
    
    char buffer[256] = {0};
    size_t len = 0;
    
    while (is_alphanumeric(lexer->current_char)) {
        buffer[len++] = lexer->current_char;
        lexer_advance(lexer);
    }
    
    buffer[len] = '\0';
    token.lexeme_length = len;
    
    /* Allocate memory for lexeme */
    token.lexeme = (char *)ocl_malloc(len + 1);
    strcpy((char *)token.lexeme, buffer);
    
    /* Check for keywords */
    if (strcmp(buffer, "Let") == 0) token.type = TOKEN_LET;
    else if (strcmp(buffer, "func") == 0) token.type = TOKEN_FUNC;
    else if (strcmp(buffer, "return") == 0) token.type = TOKEN_RETURN;
    else if (strcmp(buffer, "if") == 0) token.type = TOKEN_IF;
    else if (strcmp(buffer, "else") == 0) token.type = TOKEN_ELSE;
    else if (strcmp(buffer, "for") == 0) token.type = TOKEN_FOR;
    else if (strcmp(buffer, "while") == 0) token.type = TOKEN_WHILE;
    else if (strcmp(buffer, "Import") == 0) token.type = TOKEN_IMPORT;
    else if (strcmp(buffer, "declare") == 0) token.type = TOKEN_DECLARE;
    else if (strcmp(buffer, "true") == 0) {
        token.type = TOKEN_TRUE;
        token.value.int_value = 1;
    } else if (strcmp(buffer, "false") == 0) {
        token.type = TOKEN_FALSE;
        token.value.int_value = 0;
    } else if (strcmp(buffer, "break") == 0) token.type = TOKEN_BREAK;
    else if (strcmp(buffer, "continue") == 0) token.type = TOKEN_CONTINUE;
    
    return token;
}

Lexer *lexer_create(const char *source, const char *filename) {
    Lexer *lexer = ocl_malloc(sizeof(Lexer));
    lexer->source = source;
    lexer->position = 0;
    lexer->read_position = 1;
    lexer->current_char = source[0];
    lexer->line = 1;
    lexer->column = 1;
    lexer->filename = filename;
    return lexer;
}

void lexer_free(Lexer *lexer) {
    if (lexer) {
        ocl_free(lexer);
    }
}

Token lexer_next_token(Lexer *lexer) {
    Token token;
    memset(&token, 0, sizeof(Token));
    
    lexer_skip_whitespace(lexer);
    
    /* Handle block comments */
    while (lexer->current_char == '/' && lexer_peek(lexer) == '#') {
        lexer_skip_block_comment(lexer);
        lexer_skip_whitespace(lexer);
    }
    
    token.location = (SourceLocation){lexer->line, lexer->column, lexer->filename};
    
    if (lexer->current_char == '\0') {
        token.type = TOKEN_EOF;
        token.lexeme = "";
        return token;
    }
    
    if (lexer->current_char == '\n') {
        token.type = TOKEN_NEWLINE;
        token.lexeme = "\\n";
        lexer_advance(lexer);
        return token;
    }
    
    /* String and char literals */
    if (lexer->current_char == '"') {
        return lexer_read_string(lexer);
    }
    
    if (lexer->current_char == '\'') {
        token = lexer_read_string(lexer);
        token.type = TOKEN_CHAR;
        return token;
    }
    
    /* Numbers */
    if (is_digit(lexer->current_char)) {
        return lexer_read_number(lexer);
    }
    
    /* Identifiers and keywords */
    if (is_alpha(lexer->current_char)) {
        return lexer_read_identifier(lexer);
    }
    
    /* Operators and delimiters */
    switch (lexer->current_char) {
        case '+':
            token.type = TOKEN_PLUS;
            token.lexeme = "+";
            lexer_advance(lexer);
            break;
        case '-':
            token.type = TOKEN_MINUS;
            if (lexer_peek(lexer) == '>') {
                token.type = TOKEN_ARROW;
                token.lexeme = "->";
                lexer_advance(lexer);
            } else {
                token.lexeme = "-";
            }
            lexer_advance(lexer);
            break;
        case '*':
            token.type = TOKEN_STAR;
            token.lexeme = "*";
            lexer_advance(lexer);
            break;
        case '/':
            token.type = TOKEN_SLASH;
            token.lexeme = "/";
            lexer_advance(lexer);
            break;
        case '%':
            token.type = TOKEN_PERCENT;
            token.lexeme = "%";
            lexer_advance(lexer);
            break;
        case '=':
            if (lexer_peek(lexer) == '=') {
                token.type = TOKEN_EQUAL_EQUAL;
                token.lexeme = "==";
                lexer_advance(lexer);
            } else {
                token.type = TOKEN_EQUAL;
                token.lexeme = "=";
            }
            lexer_advance(lexer);
            break;
        case '!':
            if (lexer_peek(lexer) == '=') {
                token.type = TOKEN_BANG_EQUAL;
                token.lexeme = "!=";
                lexer_advance(lexer);
            } else {
                token.type = TOKEN_BANG;
                token.lexeme = "!";
            }
            lexer_advance(lexer);
            break;
        case '<':
            if (lexer_peek(lexer) == '=') {
                token.type = TOKEN_LESS_EQUAL;
                token.lexeme = "<=";
                lexer_advance(lexer);
            } else {
                token.type = TOKEN_LESS;
                token.lexeme = "<";
            }
            lexer_advance(lexer);
            break;
        case '>':
            if (lexer_peek(lexer) == '=') {
                token.type = TOKEN_GREATER_EQUAL;
                token.lexeme = ">=";
                lexer_advance(lexer);
            } else {
                token.type = TOKEN_GREATER;
                token.lexeme = ">";
            }
            lexer_advance(lexer);
            break;
        case '&':
            if (lexer_peek(lexer) == '&') {
                token.type = TOKEN_AMPERSAND_AMPERSAND;
                token.lexeme = "&&";
                lexer_advance(lexer);
            } else {
                token.type = TOKEN_ERROR;
                token.lexeme = "&";
            }
            lexer_advance(lexer);
            break;
        case '|':
            if (lexer_peek(lexer) == '|') {
                token.type = TOKEN_PIPE_PIPE;
                token.lexeme = "||";
                lexer_advance(lexer);
            } else {
                token.type = TOKEN_ERROR;
                token.lexeme = "|";
            }
            lexer_advance(lexer);
            break;
        case ':':
            token.type = TOKEN_COLON;
            token.lexeme = ":";
            lexer_advance(lexer);
            break;
        case ';':
            token.type = TOKEN_SEMICOLON;
            token.lexeme = ";";
            lexer_advance(lexer);
            break;
        case '.':
            token.type = TOKEN_DOT;
            token.lexeme = ".";
            lexer_advance(lexer);
            break;
        case ',':
            token.type = TOKEN_COMMA;
            token.lexeme = ",";
            lexer_advance(lexer);
            break;
        case '(':
            token.type = TOKEN_LPAREN;
            token.lexeme = "(";
            lexer_advance(lexer);
            break;
        case ')':
            token.type = TOKEN_RPAREN;
            token.lexeme = ")";
            lexer_advance(lexer);
            break;
        case '{':
            token.type = TOKEN_LBRACE;
            token.lexeme = "{";
            lexer_advance(lexer);
            break;
        case '}':
            token.type = TOKEN_RBRACE;
            token.lexeme = "}";
            lexer_advance(lexer);
            break;
        case '[':
            token.type = TOKEN_LBRACKET;
            token.lexeme = "[";
            lexer_advance(lexer);
            break;
        case ']':
            token.type = TOKEN_RBRACKET;
            token.lexeme = "]";
            lexer_advance(lexer);
            break;
        default:
            token.type = TOKEN_ERROR;
            token.lexeme = "?";
            lexer_advance(lexer);
    }
    
    token.lexeme_length = strlen(token.lexeme);
    return token;
}

Token *lexer_tokenize_all(Lexer *lexer, size_t *token_count) {
    Token *tokens = NULL;
    size_t count = 0;
    size_t capacity = 100;
    
    tokens = ocl_malloc(capacity * sizeof(Token));
    
    while (true) {
        Token token = lexer_next_token(lexer);
        
        if (count >= capacity) {
            capacity *= 2;
            tokens = ocl_realloc(tokens, capacity * sizeof(Token));
        }
        
        tokens[count++] = token;
        
        if (token.type == TOKEN_EOF) {
            break;
        }
    }
    
    *token_count = count;
    return tokens;
}
