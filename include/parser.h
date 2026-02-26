#ifndef OCL_PARSER_H
#define OCL_PARSER_H

#include "lexer.h"
#include "ast.h"
#include "errors.h"

/* ── Parser ──────────────────────────────────────────────────────── */
typedef struct {
    Token          *tokens;
    size_t          token_count;
    size_t          current;
    const char     *filename;
    ErrorCollector *errors;     /* NULL → fall back to stderr */
} Parser;

Parser      *parser_create(Token *tokens, size_t token_count,
                            const char *filename, ErrorCollector *errors);
void         parser_free(Parser *parser);
ProgramNode *parser_parse(Parser *parser);

#endif /* OCL_PARSER_H */