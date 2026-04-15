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
    AST_PROGRAM,
    AST_STRUCT_DECL,
    AST_VAR_DECL,
    AST_FUNC_DECL,
    AST_BLOCK,
    AST_IF_STMT,
    AST_FOR_LOOP,
    AST_WHILE_LOOP,
    AST_DO_WHILE_LOOP,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_BIN_OP,
    AST_UNARY_OP,
    AST_CALL,
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_IMPORT,
    AST_DECLARE,
    AST_ARRAY_LITERAL,
    AST_INDEX_ACCESS,
    AST_STRUCT_LITERAL,
    AST_FIELD_ACCESS,
    AST_TERNARY,
    AST_FUNC_EXPR,
} ASTNodeType;

/* OCL's built-in type system. */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_FUNCTION,
    TYPE_VOID,
    TYPE_UNKNOWN
} BuiltinType;

typedef enum {
    INTEGER_KIND_NONE,
    INTEGER_KIND_LITERAL,
    INTEGER_KIND_GENERIC_INT,
    INTEGER_KIND_ICHAR,
    INTEGER_KIND_SHORT,
    INTEGER_KIND_INT,
    INTEGER_KIND_LONG,
    INTEGER_KIND_INT128,
    INTEGER_KIND_IPTR,
    INTEGER_KIND_ISZ,
    INTEGER_KIND_CHAR,
    INTEGER_KIND_USHORT,
    INTEGER_KIND_UINT,
    INTEGER_KIND_ULONG,
    INTEGER_KIND_UINT128,
    INTEGER_KIND_UPTR,
    INTEGER_KIND_USZ,
} IntegerKind;

typedef struct {
    IntegerKind kind;
    const char *name;
    bool        is_signed;
    uint16_t    bits;
    bool        is_host_dependent;
} IntegerTypeInfo;

typedef struct TypeNode {
    BuiltinType       type;
    IntegerKind       integer_kind;
    struct TypeNode  *element_type;
    char             *struct_name;
    struct TypeNode **param_types;
    size_t            param_count;
    struct TypeNode  *return_type;
} TypeNode;

typedef struct ASTNode         ASTNode;
typedef struct ProgramNode     ProgramNode;
typedef struct VarDeclNode     VarDeclNode;
typedef struct StructDeclNode  StructDeclNode;
typedef struct FuncDeclNode    FuncDeclNode;
typedef struct BlockNode       BlockNode;
typedef struct IfStmtNode      IfStmtNode;
typedef struct LoopNode        LoopNode;
typedef struct ReturnNode      ReturnNode;
typedef union  ExprNode        ExprNode;
typedef struct BinOpNode       BinOpNode;
typedef struct UnaryOpNode     UnaryOpNode;
typedef struct TernaryNode     TernaryNode;
typedef struct CallNode        CallNode;
typedef struct LiteralNode     LiteralNode;
typedef struct IdentifierNode  IdentifierNode;
typedef struct ParamNode       ParamNode;
typedef struct ImportNode      ImportNode;
typedef struct DeclareNode     DeclareNode;
typedef struct StructLiteralNode StructLiteralNode;
typedef struct FieldAccessNode FieldAccessNode;
typedef struct FuncExprNode FuncExprNode;

struct ASTNode {
    ASTNodeType    type;
    SourceLocation location;
};

struct VarDeclNode {
    ASTNode   base;
    char     *name;
    TypeNode *type;
    ExprNode *initializer;
};

struct ParamNode {
    char          *name;
    TypeNode      *type;
    SourceLocation location;
};

struct StructDeclNode {
    ASTNode    base;
    char      *name;
    ParamNode **fields;
    size_t     field_count;
};

struct FuncDeclNode {
    ASTNode    base;
    char      *name;
    TypeNode  *return_type;
    ParamNode **params;
    size_t     param_count;
    BlockNode *body;
};

struct BlockNode {
    ASTNode   base;
    ASTNode **statements;
    size_t    statement_count;
};

struct IfStmtNode {
    ASTNode    base;
    ExprNode  *condition;
    BlockNode *then_block;
    ASTNode   *else_next;
};

struct LoopNode {
    ASTNode    base;
    bool       is_for;
    bool       is_do_while;
    ASTNode   *init;
    ExprNode  *condition;
    ASTNode   *increment;
    BlockNode *body;
};

struct ReturnNode {
    ASTNode   base;
    ExprNode *value;
};

struct BinOpNode {
    ASTNode    base;
    ExprNode  *left;
    ExprNode  *right;
    const char *operator;
};

struct UnaryOpNode {
    ASTNode    base;
    ExprNode  *operand;
    const char *operator;
};

struct TernaryNode {
    ASTNode    base;
    ExprNode  *condition;
    ExprNode  *true_expr;
    ExprNode  *false_expr;
};

struct CallNode {
    ASTNode    base;
    ExprNode  *callee;
    ExprNode **arguments;
    size_t     argument_count;
};

struct LiteralNode {
    ASTNode   base;
    ValueType value_type;
    Value     value;
};

struct IdentifierNode {
    ASTNode base;
    char   *name;
};

struct ImportNode {
    ASTNode base;
    char   *filename;
};

struct DeclareNode {
    ASTNode   base;
    char     *name;
    TypeNode *type;
};

typedef struct {
    ASTNode   base;
    ExprNode **elements;
    size_t     element_count;
} ArrayLiteralNode;

typedef struct {
    ASTNode   base;
    ExprNode *array_expr;
    ExprNode *index_expr;
} IndexAccessNode;

struct StructLiteralNode {
    ASTNode    base;
    char      *struct_name;
    char     **field_names;
    ExprNode **field_values;
    size_t     field_count;
};

struct FieldAccessNode {
    ASTNode   base;
    ExprNode *object;
    char     *field_name;
    bool      is_optional;
};

struct FuncExprNode {
    ASTNode    base;
    TypeNode  *return_type;
    ParamNode **params;
    size_t     param_count;
    BlockNode *body;
    char      *generated_name;
};

union ExprNode {
    ASTNode           base;
    BinOpNode         bin_op;
    UnaryOpNode       unary_op;
    TernaryNode       ternary;
    CallNode          call;
    LiteralNode       literal;
    IdentifierNode    identifier;
    ArrayLiteralNode  array_literal;
    IndexAccessNode   index_access;
    StructLiteralNode struct_literal;
    FieldAccessNode   field_access;
    FuncExprNode      func_expr;
};

struct ProgramNode {
    ASTNode       base;
    ASTNode     **nodes;
    size_t        node_count;
    ProgramNode **imports;
    size_t        import_count;
    char         *module_path;
};

ASTNode   *ast_create_var_decl(SourceLocation loc, char *name, TypeNode *type, ExprNode *initializer);
ASTNode   *ast_create_struct_decl(SourceLocation loc, char *name, ParamNode **fields, size_t field_count);
ASTNode   *ast_create_func_decl(SourceLocation loc, char *name, TypeNode *return_type,
                                ParamNode **params, size_t param_count, BlockNode *body);
BlockNode *ast_create_block(SourceLocation loc);
void       ast_add_statement(BlockNode *block, ASTNode *stmt);
ASTNode   *ast_create_if_stmt(SourceLocation loc, ExprNode *cond,
                              BlockNode *then_block, ASTNode *else_next);
ASTNode   *ast_create_return(SourceLocation loc, ExprNode *value);

ExprNode  *ast_create_binary_op(SourceLocation loc, ExprNode *left, const char *op, ExprNode *right);
ExprNode  *ast_create_ternary(SourceLocation loc, ExprNode *condition, ExprNode *true_expr, ExprNode *false_expr);
ExprNode  *ast_create_call(SourceLocation loc, ExprNode *callee, ExprNode **args, size_t arg_count);
ExprNode  *ast_create_literal(SourceLocation loc, Value value);
ExprNode  *ast_create_identifier(SourceLocation loc, char *name);
ExprNode  *ast_create_array_literal(SourceLocation loc, ExprNode **elements, size_t count);
ExprNode  *ast_create_index_access(SourceLocation loc, ExprNode *array_expr, ExprNode *index_expr);
ExprNode  *ast_create_struct_literal(SourceLocation loc, char *struct_name,
                                     char **field_names, ExprNode **field_values, size_t field_count);
ExprNode  *ast_create_field_access(SourceLocation loc, ExprNode *object, char *field_name, bool is_optional);
ExprNode  *ast_create_func_expr(SourceLocation loc, TypeNode *return_type,
                                ParamNode **params, size_t param_count, BlockNode *body);
ExprNode  *ast_clone_expr(const ExprNode *expr);

TypeNode  *ast_create_type(BuiltinType type);
TypeNode  *ast_create_type_named(BuiltinType type, const char *struct_name);
TypeNode  *ast_create_integer_type(IntegerKind kind);
const IntegerTypeInfo *ast_integer_type_info(IntegerKind kind);
bool       ast_type_is_integer(const TypeNode *type);
const char *ast_type_name(const TypeNode *type);
ParamNode *ast_create_param(char *name, TypeNode *type, SourceLocation loc);

void       ast_free(ASTNode *node);

#endif
