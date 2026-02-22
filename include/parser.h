#ifndef OCL_PARSER_H
#define OCL_PARSER_H

#include "lexer.h"
#include "ast.h"

/* Parser structure */
typedef struct {
    Token *tokens;
    size_t token_count;
    size_t current;
    const char *filename;
} Parser;

/* Parser functions */
Parser *parser_create(Token *tokens, size_t token_count, const char *filename);
void parser_free(Parser *parser);
ProgramNode *parser_parse(Parser *parser);

#endif /* OCL_PARSER_H */
