#ifndef OCL_AST_H
#define OCL_AST_H

#include "common.h"

/* Forward declarations */
typedef struct ASTNode ASTNode;
typedef struct VarDeclNode VarDeclNode;
typedef struct FuncDeclNode FuncDeclNode;
typedef struct BlockNode BlockNode;
typedef struct IfStmtNode IfStmtNode;
typedef struct LoopNode LoopNode;
typedef struct ReturnNode ReturnNode;
typedef union ExprNode ExprNode;
typedef struct BinOpNode BinOpNode;
typedef struct UnaryOpNode UnaryOpNode;
typedef struct CallNode CallNode;
typedef struct LiteralNode LiteralNode;
typedef struct IdentifierNode IdentifierNode;
typedef struct TypeNode TypeNode;
typedef struct ParamNode ParamNode;
typedef struct ImportNode ImportNode;

/* AST node types */
typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_FUNC_DECL,
    AST_BLOCK,
    AST_IF_STMT,
    AST_FOR_LOOP,
    AST_WHILE_LOOP,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_EXPR_STMT,
    AST_BIN_OP,
    AST_UNARY_OP,
    AST_CALL,
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_IMPORT,
    AST_DECLARE,
    AST_ARRAY_LITERAL,
    AST_INDEX_ACCESS,
} ASTNodeType;

/* Type information */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_ARRAY,
    TYPE_VOID,
    TYPE_UNKNOWN
} BuiltinType;

struct TypeNode {
    BuiltinType type;
    int bit_width;  /* 32 or 64, 0 = default */
    struct TypeNode *element_type;  /* For arrays */
    bool is_array;
};

/* Base AST node */
struct ASTNode {
    ASTNodeType type;
    SourceLocation location;
};

/* Variable declaration: Let x:Int = 42 */
struct VarDeclNode {
    ASTNode base;
    char *name;
    TypeNode *type;
    ExprNode *initializer;
};

/* Function parameter */
struct ParamNode {
    char *name;
    TypeNode *type;
    SourceLocation location;
};

/* Function declaration: func int add(a:int, b:int) { ... } */
struct FuncDeclNode {
    ASTNode base;
    char *name;
    TypeNode *return_type;
    ParamNode **params;
    size_t param_count;
    BlockNode *body;
};

/* Block: { statements... } */
struct BlockNode {
    ASTNode base;
    ASTNode **statements;
    size_t statement_count;
};

/* If statement */
struct IfStmtNode {
    ASTNode base;
    ExprNode *condition;
    BlockNode *then_block;
    BlockNode *else_block;  /* NULL if no else */
};

/* Loop (for/while) */
struct LoopNode {
    ASTNode base;
    bool is_for;  /* true = for, false = while */
    
    /* For loops */
    ASTNode *init;  /* Variable declaration or expression */
    ExprNode *condition;
    ASTNode *increment;  /* Expression statement */
    
    BlockNode *body;
};

/* Return statement */
struct ReturnNode {
    ASTNode base;
    ExprNode *value;  /* NULL for void return */
};

/* Binary operation: a + b */
struct BinOpNode {
    ASTNode base;
    ExprNode *left;
    ExprNode *right;
    const char *operator;  /* "+", "-", "*", "/", "==", etc. */
};

/* Unary operation: -x, !b */
struct UnaryOpNode {
    ASTNode base;
    ExprNode *operand;
    const char *operator;  /* "-", "!" */
};

/* Function call: foo(a, b) */
struct CallNode {
    ASTNode base;
    char *function_name;
    ExprNode **arguments;
    size_t argument_count;
};

/* Literal value: 42, "hello", true, 3.14 */
struct LiteralNode {
    ASTNode base;
    ValueType value_type;
    Value value;
};

/* Identifier: variable name */
struct IdentifierNode {
    ASTNode base;
    char *name;
};

/* Import statement: Import <CoreSX.sxh> */
struct ImportNode {
    ASTNode base;
    char *filename;
};

/* Base expression node type (unifies all expressions) */
union ExprNode {
    ASTNode base;
    BinOpNode bin_op;
    UnaryOpNode unary_op;
    CallNode call;
    LiteralNode literal;
    IdentifierNode identifier;
    struct {
        ASTNode base;
        ExprNode *elements;
        size_t element_count;
    } array_literal;
    struct {
        ASTNode base;
        ExprNode *array;
        ExprNode *index;
    } index_access;
};

/* Program root */
typedef struct {
    ASTNode base;
    ASTNode **nodes;
    size_t node_count;
} ProgramNode;

/* AST utilities */
ASTNode *ast_create_var_decl(SourceLocation loc, char *name, TypeNode *type, ExprNode *initializer);
ASTNode *ast_create_func_decl(SourceLocation loc, char *name, TypeNode *return_type, ParamNode **params, size_t param_count, BlockNode *body);
BlockNode *ast_create_block(SourceLocation loc);
void ast_add_statement(BlockNode *block, ASTNode *stmt);
ASTNode *ast_create_if_stmt(SourceLocation loc, ExprNode *cond, BlockNode *then_block, BlockNode *else_block);
ASTNode *ast_create_return(SourceLocation loc, ExprNode *value);
ExprNode *ast_create_binary_op(SourceLocation loc, ExprNode *left, const char *op, ExprNode *right);
ExprNode *ast_create_call(SourceLocation loc, char *name, ExprNode **args, size_t arg_count);
ExprNode *ast_create_literal(SourceLocation loc, Value value);
ExprNode *ast_create_identifier(SourceLocation loc, char *name);
TypeNode *ast_create_type(BuiltinType type, int bit_width);
ParamNode *ast_create_param(char *name, TypeNode *type, SourceLocation loc);
void ast_free(ASTNode *node);

#endif /* OCL_AST_H */
