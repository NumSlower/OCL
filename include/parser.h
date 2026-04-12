#ifndef OCL_PARSER_H
#define OCL_PARSER_H

#include "ast.h"
#include "errors.h"
#include "lexer.h"

typedef struct ParseState ParseState;

typedef struct {
    Token          *tokens;
    size_t          token_count;
    size_t          current;
    const char     *filename;
    ErrorCollector *errors;
    ParseState     *state;
    bool            owns_state;
} Parser;

Parser      *parser_create(Token *tokens, size_t token_count, const char *filename, ErrorCollector *errors);
void         parser_free(Parser *parser);
ProgramNode *parser_parse(Parser *parser);

#endif
