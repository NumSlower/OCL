#ifndef OCL_AST_H
#define OCL_AST_H

#include "common.h"

/*
 * AST node type tags.
 *
 * Every ASTNode begins with an `ASTNode base` field so any node pointer
 * can be safely cast to ASTNode* to read its type and source location.
 */
typedef enum {
    AST_PROGRAM,      /* top-level program — container for all top-level nodes */
    AST_VAR_DECL,     /* Let x:T = expr  or  T x = expr                        */
    AST_FUNC_DECL,    /* func [RetType] name(params) { body }                  */
    AST_BLOCK,        /* { stmt* }                                              */
    AST_IF_STMT,      /* if (cond) { } [else if ...] [else { }]                */
    AST_FOR_LOOP,     /* for (init; cond; incr) { }                            */
    AST_WHILE_LOOP,   /* while (cond) { }                                      */
    AST_RETURN,       /* return [expr]                                          */
    AST_BREAK,        /* break                                                  */
    AST_CONTINUE,     /* continue                                               */
    AST_BIN_OP,       /* expr op expr                                           */
    AST_UNARY_OP,     /* op expr                                                */
    AST_CALL,         /* name(args)                                             */
    AST_LITERAL,      /* integer, float, string, bool, or char literal          */
    AST_IDENTIFIER,   /* variable or function reference                         */
    AST_IMPORT,       /* Import <file> (resolved at parse time)                 */
    AST_DECLARE,      /* declare name:Type  (forward declaration for checker)   */
    AST_ARRAY_LITERAL,/* [elem, ...]                                            */
    AST_INDEX_ACCESS, /* expr[expr]                                             */
} ASTNodeType;

/* OCL's built-in type system (used by the parser and type checker). */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_ARRAY,
    TYPE_VOID,
    TYPE_UNKNOWN  /* used as a placeholder when the type is not yet resolved */
} BuiltinType;

/*
 * TypeNode — describes a type annotation in the source.
 * All integers are 64-bit at runtime regardless of any bit-width suffix.
 */
typedef struct TypeNode {
    BuiltinType  type;
    struct TypeNode *element_type; /* reserved for future array element types */
} TypeNode;

/* ── AST node structs ─────────────────────────────────────────────── */

typedef struct ASTNode     ASTNode;
typedef struct VarDeclNode VarDeclNode;
typedef struct FuncDeclNode FuncDeclNode;
typedef struct BlockNode   BlockNode;
typedef struct IfStmtNode  IfStmtNode;
typedef struct LoopNode    LoopNode;
typedef struct ReturnNode  ReturnNode;
typedef union  ExprNode    ExprNode;
typedef struct BinOpNode   BinOpNode;
typedef struct UnaryOpNode UnaryOpNode;
typedef struct CallNode    CallNode;
typedef struct LiteralNode LiteralNode;
typedef struct IdentifierNode IdentifierNode;
typedef struct ParamNode   ParamNode;
typedef struct ImportNode  ImportNode;
typedef struct DeclareNode DeclareNode;

/* Base: every node starts with these two fields. */
struct ASTNode { ASTNodeType type; SourceLocation location; };

/* Variable declaration: Let x:T = init  or  T x = init */
struct VarDeclNode  { ASTNode base; char *name; TypeNode *type; ExprNode *initializer; };

/* Function parameter. */
struct ParamNode    { char *name; TypeNode *type; SourceLocation location; };

/* Function declaration. */
struct FuncDeclNode {
    ASTNode    base;
    char      *name;
    TypeNode  *return_type;
    ParamNode **params;
    size_t     param_count;
    BlockNode *body;
};

/* A sequence of statements. */
struct BlockNode    { ASTNode base; ASTNode **statements; size_t statement_count; };

/*
 * If/else-if/else chain.
 *
 * `else_next` points to the next IfStmtNode (else-if) or a BlockNode
 * (plain else), or NULL (no else).  This avoids synthetic wrapper nodes.
 */
struct IfStmtNode {
    ASTNode    base;
    ExprNode  *condition;
    BlockNode *then_block;
    ASTNode   *else_next;
};

/*
 * Loop — shared struct for both while and for loops.
 * `is_for` distinguishes them.
 */
struct LoopNode {
    ASTNode    base;
    bool       is_for;
    ASTNode   *init;       /* for only: init statement (may be a VarDeclNode) */
    ExprNode  *condition;
    ASTNode   *increment;  /* for only: post-body expression                   */
    BlockNode *body;
};

struct ReturnNode   { ASTNode base; ExprNode *value; };   /* value may be NULL for void return */

/* Binary expression: left op right */
struct BinOpNode    { ASTNode base; ExprNode *left; ExprNode *right; const char *operator; };

/* Unary expression: op operand */
struct UnaryOpNode  { ASTNode base; ExprNode *operand; const char *operator; };

/* Function call: name(arg0, arg1, ...) */
struct CallNode     { ASTNode base; char *function_name; ExprNode **arguments; size_t argument_count; };

/* Literal value (integer, float, string, bool, char). */
struct LiteralNode  { ASTNode base; ValueType value_type; Value value; };

/* Variable / function reference by name. */
struct IdentifierNode { ASTNode base; char *name; };

/* Import <filename> — resolved at parse time; codegen ignores it. */
struct ImportNode   { ASTNode base; char *filename; };

/* declare name:Type — forward-declare without initialiser. */
struct DeclareNode  { ASTNode base; char *name; TypeNode *type; };

/* Array literal: [ elem0, elem1, ... ] */
typedef struct {
    ASTNode   base;
    ExprNode **elements;
    size_t     element_count;
} ArrayLiteralNode;

/* Subscript access: array_expr[index_expr] */
typedef struct {
    ASTNode   base;
    ExprNode *array_expr;
    ExprNode *index_expr;
} IndexAccessNode;

/*
 * ExprNode — union of all expression node types.
 * Every member starts with an ASTNode so the tag is always at offset 0.
 */
union ExprNode {
    ASTNode        base;
    BinOpNode      bin_op;
    UnaryOpNode    unary_op;
    CallNode       call;
    LiteralNode    literal;
    IdentifierNode identifier;
    ArrayLiteralNode array_literal;
    IndexAccessNode  index_access;
};

/* Top-level program node: an ordered list of top-level declarations. */
typedef struct { ASTNode base; ASTNode **nodes; size_t node_count; } ProgramNode;

/* ── AST construction helpers ─────────────────────────────────────── */

ASTNode   *ast_create_var_decl(SourceLocation loc, char *name, TypeNode *type, ExprNode *initializer);
ASTNode   *ast_create_func_decl(SourceLocation loc, char *name, TypeNode *return_type,
                                 ParamNode **params, size_t param_count, BlockNode *body);
BlockNode *ast_create_block(SourceLocation loc);
void       ast_add_statement(BlockNode *block, ASTNode *stmt);
ASTNode   *ast_create_if_stmt(SourceLocation loc, ExprNode *cond,
                               BlockNode *then_block, ASTNode *else_next);
ASTNode   *ast_create_return(SourceLocation loc, ExprNode *value);

ExprNode  *ast_create_binary_op(SourceLocation loc, ExprNode *left, const char *op, ExprNode *right);
ExprNode  *ast_create_call(SourceLocation loc, char *name, ExprNode **args, size_t arg_count);
ExprNode  *ast_create_literal(SourceLocation loc, Value value);
ExprNode  *ast_create_identifier(SourceLocation loc, char *name);
ExprNode  *ast_create_array_literal(SourceLocation loc, ExprNode **elements, size_t count);
ExprNode  *ast_create_index_access(SourceLocation loc, ExprNode *array_expr, ExprNode *index_expr);

TypeNode  *ast_create_type(BuiltinType type);
ParamNode *ast_create_param(char *name, TypeNode *type, SourceLocation loc);

/* Recursively free an AST node and all its children. */
void       ast_free(ASTNode *node);

#endif /* OCL_AST_H */