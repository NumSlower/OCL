#include "ast.h"
#include "common.h"

#include <string.h>

static TypeNode *ast_clone_type(const TypeNode *type);
static const IntegerTypeInfo *integer_type_info_impl(IntegerKind kind) {
    static bool initialised = false;
    static IntegerTypeInfo table[] = {
        { INTEGER_KIND_NONE,        NULL,     true,  0, false },
        { INTEGER_KIND_LITERAL,     "<int>",  true,  0, false },
        { INTEGER_KIND_GENERIC_INT, "Int",    true, 64, false },
        { INTEGER_KIND_ICHAR,       "ichar",  true,  8, false },
        { INTEGER_KIND_SHORT,       "short",  true, 16, false },
        { INTEGER_KIND_INT,         "int",    true, 32, false },
        { INTEGER_KIND_LONG,        "long",   true, 64, false },
        { INTEGER_KIND_INT128,      "int128", true, 128, false },
        { INTEGER_KIND_IPTR,        "iptr",   true,  0, true  },
        { INTEGER_KIND_ISZ,         "isz",    true,  0, true  },
        { INTEGER_KIND_CHAR,        "char",   false, 8, false },
        { INTEGER_KIND_USHORT,      "ushort", false, 16, false },
        { INTEGER_KIND_UINT,        "uint",   false, 32, false },
        { INTEGER_KIND_ULONG,       "ulong",  false, 64, false },
        { INTEGER_KIND_UINT128,     "uint128",false,128, false },
        { INTEGER_KIND_UPTR,        "uptr",   false, 0, true  },
        { INTEGER_KIND_USZ,         "usz",    false, 0, true  },
    };

    if (!initialised) {
        table[INTEGER_KIND_IPTR].bits = (uint16_t)(sizeof(intptr_t) * 8u);
        table[INTEGER_KIND_ISZ].bits = (uint16_t)(sizeof(ptrdiff_t) * 8u);
        table[INTEGER_KIND_UPTR].bits = (uint16_t)(sizeof(uintptr_t) * 8u);
        table[INTEGER_KIND_USZ].bits = (uint16_t)(sizeof(size_t) * 8u);
        initialised = true;
    }

    if ((size_t)kind >= (sizeof(table) / sizeof(table[0])))
        return &table[INTEGER_KIND_NONE];
    return &table[(size_t)kind];
}

static ASTNode *ast_clone_node(const ASTNode *node);
ExprNode *ast_clone_expr(const ExprNode *expr);

static void ast_free_type(TypeNode *type) {
    if (!type) return;
    ast_free_type(type->element_type);
    ocl_free(type->struct_name);
    if (type->param_types) {
        for (size_t i = 0; i < type->param_count; i++)
            ast_free_type(type->param_types[i]);
        ocl_free(type->param_types);
    }
    ast_free_type(type->return_type);
    ocl_free(type);
}

static TypeNode *ast_clone_type(const TypeNode *type) {
    if (!type) return NULL;

    TypeNode *copy = ast_create_type_named(type->type, type->struct_name);
    copy->integer_kind = type->integer_kind;
    copy->element_type = ast_clone_type(type->element_type);
    copy->param_count = type->param_count;
    if (type->param_count > 0) {
        copy->param_types = ocl_malloc(type->param_count * sizeof(TypeNode *));
        for (size_t i = 0; i < type->param_count; i++)
            copy->param_types[i] = ast_clone_type(type->param_types[i]);
    }
    copy->return_type = ast_clone_type(type->return_type);
    return copy;
}

static BlockNode *ast_clone_block(const BlockNode *block) {
    if (!block) return NULL;

    BlockNode *copy = ast_create_block(block->base.location);
    for (size_t i = 0; i < block->statement_count; i++)
        ast_add_statement(copy, ast_clone_node(block->statements[i]));
    return copy;
}

static ASTNode *ast_clone_node(const ASTNode *node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_VAR_DECL: {
            const VarDeclNode *v = (const VarDeclNode *)node;
            return ast_create_var_decl(v->base.location,
                                       ocl_strdup(v->name),
                                       ast_clone_type(v->type),
                                       ast_clone_expr(v->initializer));
        }
        case AST_BLOCK:
            return (ASTNode *)ast_clone_block((const BlockNode *)node);
        case AST_RETURN: {
            const ReturnNode *r = (const ReturnNode *)node;
            return ast_create_return(r->base.location, ast_clone_expr(r->value));
        }
        case AST_DECLARE: {
            const DeclareNode *d = (const DeclareNode *)node;
            DeclareNode *copy = ocl_malloc(sizeof(DeclareNode));
            copy->base.type = AST_DECLARE;
            copy->base.location = d->base.location;
            copy->name = ocl_strdup(d->name);
            copy->type = ast_clone_type(d->type);
            return (ASTNode *)copy;
        }
        case AST_IF_STMT: {
            const IfStmtNode *s = (const IfStmtNode *)node;
            return ast_create_if_stmt(s->base.location,
                                      ast_clone_expr(s->condition),
                                      ast_clone_block(s->then_block),
                                      ast_clone_node(s->else_next));
        }
        case AST_FOR_LOOP:
        case AST_WHILE_LOOP:
        case AST_DO_WHILE_LOOP: {
            const LoopNode *lp = (const LoopNode *)node;
            LoopNode *copy = ocl_malloc(sizeof(LoopNode));
            copy->base.type = lp->base.type;
            copy->base.location = lp->base.location;
            copy->is_for = lp->is_for;
            copy->is_do_while = lp->is_do_while;
            copy->init = ast_clone_node(lp->init);
            copy->condition = ast_clone_expr(lp->condition);
            copy->increment = ast_clone_node(lp->increment);
            copy->body = ast_clone_block(lp->body);
            return (ASTNode *)copy;
        }
        case AST_BREAK:
        case AST_CONTINUE: {
            ASTNode *copy = ocl_malloc(sizeof(ASTNode));
            *copy = *node;
            return copy;
        }
        default:
            return (ASTNode *)ast_clone_expr((const ExprNode *)node);
    }
}

ASTNode *ast_create_var_decl(SourceLocation loc, char *name, TypeNode *type, ExprNode *init) {
    VarDeclNode *n = ocl_malloc(sizeof(VarDeclNode));
    n->base.type = AST_VAR_DECL;
    n->base.location = loc;
    n->name = name;
    n->type = type;
    n->initializer = init;
    return (ASTNode *)n;
}

ASTNode *ast_create_struct_decl(SourceLocation loc, char *name, ParamNode **fields, size_t field_count) {
    StructDeclNode *n = ocl_malloc(sizeof(StructDeclNode));
    n->base.type = AST_STRUCT_DECL;
    n->base.location = loc;
    n->name = name;
    n->fields = fields;
    n->field_count = field_count;
    return (ASTNode *)n;
}

ASTNode *ast_create_func_decl(SourceLocation loc, char *name, TypeNode *ret,
                              ParamNode **params, size_t n, BlockNode *body) {
    FuncDeclNode *fd = ocl_malloc(sizeof(FuncDeclNode));
    fd->base.type = AST_FUNC_DECL;
    fd->base.location = loc;
    fd->name = name;
    fd->return_type = ret;
    fd->params = params;
    fd->param_count = n;
    fd->body = body;
    return (ASTNode *)fd;
}

BlockNode *ast_create_block(SourceLocation loc) {
    BlockNode *b = ocl_malloc(sizeof(BlockNode));
    b->base.type = AST_BLOCK;
    b->base.location = loc;
    b->statements = NULL;
    b->statement_count = 0;
    return b;
}

void ast_add_statement(BlockNode *block, ASTNode *stmt) {
    if (!block || !stmt) return;
    block->statements = ocl_realloc(block->statements,
                                    (block->statement_count + 1) * sizeof(ASTNode *));
    block->statements[block->statement_count++] = stmt;
}

ASTNode *ast_create_if_stmt(SourceLocation loc, ExprNode *cond,
                            BlockNode *then_block, ASTNode *else_next) {
    IfStmtNode *n = ocl_malloc(sizeof(IfStmtNode));
    n->base.type = AST_IF_STMT;
    n->base.location = loc;
    n->condition = cond;
    n->then_block = then_block;
    n->else_next = else_next;
    return (ASTNode *)n;
}

ASTNode *ast_create_return(SourceLocation loc, ExprNode *value) {
    ReturnNode *n = ocl_malloc(sizeof(ReturnNode));
    n->base.type = AST_RETURN;
    n->base.location = loc;
    n->value = value;
    return (ASTNode *)n;
}

ExprNode *ast_create_binary_op(SourceLocation loc, ExprNode *left,
                               const char *op, ExprNode *right) {
    BinOpNode *n = ocl_malloc(sizeof(BinOpNode));
    n->base.type = AST_BIN_OP;
    n->base.location = loc;
    n->left = left;
    n->right = right;
    n->operator = op;
    return (ExprNode *)n;
}

ExprNode *ast_create_call(SourceLocation loc, ExprNode *callee,
                          ExprNode **args, size_t arg_count) {
    CallNode *n = ocl_malloc(sizeof(CallNode));
    n->base.type = AST_CALL;
    n->base.location = loc;
    n->callee = callee;
    n->arguments = args;
    n->argument_count = arg_count;
    return (ExprNode *)n;
}

ExprNode *ast_create_ternary(SourceLocation loc, ExprNode *condition,
                             ExprNode *true_expr, ExprNode *false_expr) {
    TernaryNode *n = ocl_malloc(sizeof(TernaryNode));
    n->base.type = AST_TERNARY;
    n->base.location = loc;
    n->condition = condition;
    n->true_expr = true_expr;
    n->false_expr = false_expr;
    return (ExprNode *)n;
}

ExprNode *ast_create_literal(SourceLocation loc, Value value) {
    LiteralNode *n = ocl_malloc(sizeof(LiteralNode));
    n->base.type = AST_LITERAL;
    n->base.location = loc;
    n->value_type = value.type;
    n->value = value;
    return (ExprNode *)n;
}

ExprNode *ast_create_identifier(SourceLocation loc, char *name) {
    IdentifierNode *n = ocl_malloc(sizeof(IdentifierNode));
    n->base.type = AST_IDENTIFIER;
    n->base.location = loc;
    n->name = name;
    return (ExprNode *)n;
}

ExprNode *ast_create_array_literal(SourceLocation loc,
                                   ExprNode **elements, size_t count) {
    ArrayLiteralNode *n = ocl_malloc(sizeof(ArrayLiteralNode));
    n->base.type = AST_ARRAY_LITERAL;
    n->base.location = loc;
    n->elements = elements;
    n->element_count = count;
    return (ExprNode *)n;
}

ExprNode *ast_create_index_access(SourceLocation loc,
                                  ExprNode *array_expr, ExprNode *index_expr) {
    IndexAccessNode *n = ocl_malloc(sizeof(IndexAccessNode));
    n->base.type = AST_INDEX_ACCESS;
    n->base.location = loc;
    n->array_expr = array_expr;
    n->index_expr = index_expr;
    return (ExprNode *)n;
}

ExprNode *ast_create_struct_literal(SourceLocation loc, char *struct_name,
                                    char **field_names, ExprNode **field_values, size_t field_count) {
    StructLiteralNode *n = ocl_malloc(sizeof(StructLiteralNode));
    n->base.type = AST_STRUCT_LITERAL;
    n->base.location = loc;
    n->struct_name = struct_name;
    n->field_names = field_names;
    n->field_values = field_values;
    n->field_count = field_count;
    return (ExprNode *)n;
}

ExprNode *ast_create_field_access(SourceLocation loc, ExprNode *object, char *field_name, bool is_optional) {
    FieldAccessNode *n = ocl_malloc(sizeof(FieldAccessNode));
    n->base.type = AST_FIELD_ACCESS;
    n->base.location = loc;
    n->object = object;
    n->field_name = field_name;
    n->is_optional = is_optional;
    return (ExprNode *)n;
}

ExprNode *ast_create_func_expr(SourceLocation loc, TypeNode *return_type,
                               ParamNode **params, size_t param_count, BlockNode *body) {
    FuncExprNode *n = ocl_malloc(sizeof(FuncExprNode));
    n->base.type = AST_FUNC_EXPR;
    n->base.location = loc;
    n->return_type = return_type;
    n->params = params;
    n->param_count = param_count;
    n->body = body;
    n->generated_name = NULL;
    return (ExprNode *)n;
}

ExprNode *ast_clone_expr(const ExprNode *expr) {
    if (!expr) return NULL;

    switch (expr->base.type) {
        case AST_BIN_OP: {
            const BinOpNode *b = (const BinOpNode *)expr;
            return ast_create_binary_op(b->base.location,
                                        ast_clone_expr(b->left),
                                        b->operator,
                                        ast_clone_expr(b->right));
        }
        case AST_UNARY_OP: {
            const UnaryOpNode *u = (const UnaryOpNode *)expr;
            UnaryOpNode *copy = ocl_malloc(sizeof(UnaryOpNode));
            copy->base.type = AST_UNARY_OP;
            copy->base.location = u->base.location;
            copy->operator = u->operator;
            copy->operand = ast_clone_expr(u->operand);
            return (ExprNode *)copy;
        }
        case AST_TERNARY: {
            const TernaryNode *t = (const TernaryNode *)expr;
            return ast_create_ternary(t->base.location,
                                      ast_clone_expr(t->condition),
                                      ast_clone_expr(t->true_expr),
                                      ast_clone_expr(t->false_expr));
        }
        case AST_CALL: {
            const CallNode *c = (const CallNode *)expr;
            ExprNode **args = NULL;
            if (c->argument_count > 0)
                args = ocl_malloc(c->argument_count * sizeof(ExprNode *));
            for (size_t i = 0; i < c->argument_count; i++)
                args[i] = ast_clone_expr(c->arguments[i]);
            return ast_create_call(c->base.location,
                                   ast_clone_expr(c->callee),
                                   args,
                                   c->argument_count);
        }
        case AST_LITERAL: {
            const LiteralNode *lit = (const LiteralNode *)expr;
            return ast_create_literal(lit->base.location, value_own_copy(lit->value));
        }
        case AST_IDENTIFIER: {
            const IdentifierNode *id = (const IdentifierNode *)expr;
            return ast_create_identifier(id->base.location, ocl_strdup(id->name));
        }
        case AST_ARRAY_LITERAL: {
            const ArrayLiteralNode *al = (const ArrayLiteralNode *)expr;
            ExprNode **elements = NULL;
            if (al->element_count > 0)
                elements = ocl_malloc(al->element_count * sizeof(ExprNode *));
            for (size_t i = 0; i < al->element_count; i++)
                elements[i] = ast_clone_expr(al->elements[i]);
            return ast_create_array_literal(al->base.location, elements, al->element_count);
        }
        case AST_INDEX_ACCESS: {
            const IndexAccessNode *ia = (const IndexAccessNode *)expr;
            return ast_create_index_access(ia->base.location,
                                           ast_clone_expr(ia->array_expr),
                                           ast_clone_expr(ia->index_expr));
        }
        case AST_STRUCT_LITERAL: {
            const StructLiteralNode *sl = (const StructLiteralNode *)expr;
            char **field_names = NULL;
            ExprNode **field_values = NULL;
            if (sl->field_count > 0) {
                field_names = ocl_malloc(sl->field_count * sizeof(char *));
                field_values = ocl_malloc(sl->field_count * sizeof(ExprNode *));
            }
            for (size_t i = 0; i < sl->field_count; i++) {
                field_names[i] = ocl_strdup(sl->field_names[i]);
                field_values[i] = ast_clone_expr(sl->field_values[i]);
            }
            return ast_create_struct_literal(sl->base.location,
                                             ocl_strdup(sl->struct_name),
                                             field_names,
                                             field_values,
                                             sl->field_count);
        }
        case AST_FIELD_ACCESS: {
            const FieldAccessNode *fa = (const FieldAccessNode *)expr;
            return ast_create_field_access(fa->base.location,
                                           ast_clone_expr(fa->object),
                                           ocl_strdup(fa->field_name),
                                           fa->is_optional);
        }
        case AST_FUNC_EXPR: {
            const FuncExprNode *fn = (const FuncExprNode *)expr;
            ParamNode **params = NULL;
            if (fn->param_count > 0)
                params = ocl_malloc(fn->param_count * sizeof(ParamNode *));
            for (size_t i = 0; i < fn->param_count; i++) {
                params[i] = ast_create_param(ocl_strdup(fn->params[i]->name),
                                             ast_clone_type(fn->params[i]->type),
                                             fn->params[i]->location);
            }
            ExprNode *copy = ast_create_func_expr(fn->base.location,
                                                  ast_clone_type(fn->return_type),
                                                  params,
                                                  fn->param_count,
                                                  (BlockNode *)ast_clone_block(fn->body));
            if (copy && copy->base.type == AST_FUNC_EXPR) {
                FuncExprNode *copy_fn = (FuncExprNode *)copy;
                copy_fn->generated_name = fn->generated_name ? ocl_strdup(fn->generated_name) : NULL;
            }
            return copy;
        }
        default:
            return NULL;
    }
}

TypeNode *ast_create_type_named(BuiltinType type, const char *struct_name) {
    TypeNode *t = ocl_malloc(sizeof(TypeNode));
    t->type = type;
    t->integer_kind = (type == TYPE_INT) ? INTEGER_KIND_GENERIC_INT : INTEGER_KIND_NONE;
    t->element_type = NULL;
    t->struct_name = struct_name ? ocl_strdup(struct_name) : NULL;
    t->param_types = NULL;
    t->param_count = 0;
    t->return_type = NULL;
    return t;
}

TypeNode *ast_create_type(BuiltinType type) {
    return ast_create_type_named(type, NULL);
}

TypeNode *ast_create_integer_type(IntegerKind kind) {
    TypeNode *type = ast_create_type(TYPE_INT);
    type->integer_kind = kind;
    return type;
}

const IntegerTypeInfo *ast_integer_type_info(IntegerKind kind) {
    return integer_type_info_impl(kind);
}

bool ast_type_is_integer(const TypeNode *type) {
    return type && type->type == TYPE_INT;
}

const char *ast_type_name(const TypeNode *type) {
    const IntegerTypeInfo *info;

    if (!type)
        return "Unknown";

    if (ast_type_is_integer(type)) {
        info = ast_integer_type_info(type->integer_kind);
        return info && info->name ? info->name : "Int";
    }

    switch (type->type) {
        case TYPE_FLOAT:    return "Float";
        case TYPE_STRING:   return "String";
        case TYPE_BOOL:     return "Bool";
        case TYPE_CHAR:     return "Char";
        case TYPE_ARRAY:    return "Array";
        case TYPE_STRUCT:   return type->struct_name ? type->struct_name : "Struct";
        case TYPE_FUNCTION: return "Func";
        case TYPE_VOID:     return "void";
        case TYPE_UNKNOWN:  return "Unknown";
        case TYPE_INT:      return "Int";
    }

    return "Unknown";
}

ParamNode *ast_create_param(char *name, TypeNode *type, SourceLocation loc) {
    ParamNode *p = ocl_malloc(sizeof(ParamNode));
    p->name = name;
    p->type = type;
    p->location = loc;
    return p;
}

void ast_free(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM: {
            ProgramNode *p = (ProgramNode *)node;
            for (size_t i = 0; i < p->node_count; i++)
                ast_free(p->nodes[i]);
            for (size_t i = 0; i < p->import_count; i++)
                ast_free((ASTNode *)p->imports[i]);
            ocl_free(p->nodes);
            ocl_free(p->imports);
            ocl_free(p->module_path);
            break;
        }
        case AST_STRUCT_DECL: {
            StructDeclNode *s = (StructDeclNode *)node;
            ocl_free(s->name);
            for (size_t i = 0; i < s->field_count; i++) {
                if (!s->fields[i]) continue;
                ocl_free(s->fields[i]->name);
                ast_free_type(s->fields[i]->type);
                ocl_free(s->fields[i]);
            }
            ocl_free(s->fields);
            break;
        }
        case AST_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode *)node;
            ocl_free(v->name);
            ast_free_type(v->type);
            ast_free((ASTNode *)v->initializer);
            break;
        }
        case AST_FUNC_DECL: {
            FuncDeclNode *f = (FuncDeclNode *)node;
            ocl_free(f->name);
            ast_free_type(f->return_type);
            for (size_t i = 0; i < f->param_count; i++) {
                if (!f->params[i]) continue;
                ocl_free(f->params[i]->name);
                ast_free_type(f->params[i]->type);
                ocl_free(f->params[i]);
            }
            ocl_free(f->params);
            ast_free((ASTNode *)f->body);
            break;
        }
        case AST_BLOCK: {
            BlockNode *b = (BlockNode *)node;
            for (size_t i = 0; i < b->statement_count; i++)
                ast_free(b->statements[i]);
            ocl_free(b->statements);
            break;
        }
        case AST_IF_STMT: {
            IfStmtNode *s = (IfStmtNode *)node;
            ast_free((ASTNode *)s->condition);
            ast_free((ASTNode *)s->then_block);
            ast_free(s->else_next);
            break;
        }
        case AST_FOR_LOOP:
        case AST_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            ast_free(lp->init);
            ast_free((ASTNode *)lp->condition);
            ast_free(lp->increment);
            ast_free((ASTNode *)lp->body);
            break;
        }
        case AST_DO_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            ast_free(lp->init);
            ast_free((ASTNode *)lp->condition);
            ast_free(lp->increment);
            ast_free((ASTNode *)lp->body);
            break;
        }
        case AST_RETURN: {
            ReturnNode *r = (ReturnNode *)node;
            ast_free((ASTNode *)r->value);
            break;
        }
        case AST_BIN_OP: {
            BinOpNode *b = (BinOpNode *)node;
            ast_free((ASTNode *)b->left);
            ast_free((ASTNode *)b->right);
            break;
        }
        case AST_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode *)node;
            ast_free((ASTNode *)u->operand);
            break;
        }
        case AST_TERNARY: {
            TernaryNode *t = (TernaryNode *)node;
            ast_free((ASTNode *)t->condition);
            ast_free((ASTNode *)t->true_expr);
            ast_free((ASTNode *)t->false_expr);
            break;
        }
        case AST_CALL: {
            CallNode *c = (CallNode *)node;
            ast_free((ASTNode *)c->callee);
            for (size_t i = 0; i < c->argument_count; i++)
                ast_free((ASTNode *)c->arguments[i]);
            ocl_free(c->arguments);
            break;
        }
        case AST_IDENTIFIER: {
            IdentifierNode *id = (IdentifierNode *)node;
            ocl_free(id->name);
            break;
        }
        case AST_LITERAL: {
            LiteralNode *lit = (LiteralNode *)node;
            value_free(lit->value);
            break;
        }
        case AST_IMPORT: {
            ImportNode *imp = (ImportNode *)node;
            ocl_free(imp->filename);
            break;
        }
        case AST_DECLARE: {
            DeclareNode *d = (DeclareNode *)node;
            ocl_free(d->name);
            ast_free_type(d->type);
            break;
        }
        case AST_ARRAY_LITERAL: {
            ArrayLiteralNode *al = (ArrayLiteralNode *)node;
            for (size_t i = 0; i < al->element_count; i++)
                ast_free((ASTNode *)al->elements[i]);
            ocl_free(al->elements);
            break;
        }
        case AST_INDEX_ACCESS: {
            IndexAccessNode *ia = (IndexAccessNode *)node;
            ast_free((ASTNode *)ia->array_expr);
            ast_free((ASTNode *)ia->index_expr);
            break;
        }
        case AST_STRUCT_LITERAL: {
            StructLiteralNode *sl = (StructLiteralNode *)node;
            ocl_free(sl->struct_name);
            for (size_t i = 0; i < sl->field_count; i++) {
                ocl_free(sl->field_names[i]);
                ast_free((ASTNode *)sl->field_values[i]);
            }
            ocl_free(sl->field_names);
            ocl_free(sl->field_values);
            break;
        }
        case AST_FIELD_ACCESS: {
            FieldAccessNode *fa = (FieldAccessNode *)node;
            ast_free((ASTNode *)fa->object);
            ocl_free(fa->field_name);
            break;
        }
        case AST_FUNC_EXPR: {
            FuncExprNode *fn = (FuncExprNode *)node;
            ast_free_type(fn->return_type);
            for (size_t i = 0; i < fn->param_count; i++) {
                if (!fn->params[i]) continue;
                ocl_free(fn->params[i]->name);
                ast_free_type(fn->params[i]->type);
                ocl_free(fn->params[i]);
            }
            ocl_free(fn->params);
            ast_free((ASTNode *)fn->body);
            ocl_free(fn->generated_name);
            break;
        }
        default:
            break;
    }

    ocl_free(node);
}
