#include "type_checker.h"
#include "common.h"
#include "ocl_stdlib.h"
#include <string.h>
#include <stdio.h>

SymbolTable *symbol_table_create(void) {
    SymbolTable *t = ocl_malloc(sizeof(SymbolTable));
    t->symbols = NULL; t->symbol_count = 0; t->capacity = 0; t->current_scope_level = 0;
    return t;
}

void symbol_table_free(SymbolTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->symbol_count; i++) {
        Symbol *s = t->symbols[i];
        ocl_free(s->name); ocl_free(s);
    }
    ocl_free(t->symbols); ocl_free(t);
}

void symbol_table_enter_scope(SymbolTable *t) { if (t) t->current_scope_level++; }

void symbol_table_exit_scope(SymbolTable *t) {
    if (!t) return;
    size_t i = t->symbol_count;
    while (i-- > 0) {
        if (t->symbols[i]->scope_level == t->current_scope_level) {
            Symbol *s = t->symbols[i];
            ocl_free(s->name); ocl_free(s);
            for (size_t j = i; j + 1 < t->symbol_count; j++) t->symbols[j] = t->symbols[j+1];
            t->symbol_count--;
        }
    }
    t->current_scope_level--;
}

static void _insert(SymbolTable *t, Symbol *sym) {
    if (t->symbol_count >= t->capacity) {
        t->capacity = t->capacity ? t->capacity * 2 : 16;
        t->symbols = ocl_realloc(t->symbols, t->capacity * sizeof(Symbol *));
    }
    t->symbols[t->symbol_count++] = sym;
}

void symbol_table_insert(SymbolTable *t, const char *name, TypeNode *type, bool is_function, bool is_parameter) {
    if (!t) return;
    Symbol *s = ocl_malloc(sizeof(Symbol));
    s->name = ocl_strdup(name); s->type = type; s->is_function = is_function;
    s->is_parameter = is_parameter; s->scope_level = t->current_scope_level;
    s->param_types = NULL; s->param_count = 0;
    _insert(t, s);
}

void symbol_table_insert_func(SymbolTable *t, const char *name, TypeNode *ret_type, TypeNode **param_types, int param_count) {
    if (!t) return;
    Symbol *s = ocl_malloc(sizeof(Symbol));
    s->name = ocl_strdup(name); s->type = ret_type; s->is_function = true;
    s->is_parameter = false; s->scope_level = t->current_scope_level;
    s->param_types = param_types; s->param_count = param_count;
    _insert(t, s);
}

Symbol *symbol_table_lookup(SymbolTable *t, const char *name) {
    if (!t) return NULL;
    for (int i = (int)t->symbol_count - 1; i >= 0; i--)
        if (!strcmp(t->symbols[i]->name, name)) return t->symbols[i];
    return NULL;
}

bool symbol_table_has_in_current_scope(SymbolTable *t, const char *name) {
    if (!t) return false;
    for (size_t i = 0; i < t->symbol_count; i++)
        if (t->symbols[i]->scope_level == t->current_scope_level && !strcmp(t->symbols[i]->name, name)) return true;
    return false;
}

TypeChecker *type_checker_create(ErrorCollector *errors) {
    TypeChecker *tc = ocl_malloc(sizeof(TypeChecker));
    tc->symbol_table = symbol_table_create();
    tc->current_function_return_type = NULL; tc->errors = errors; tc->error_count = 0;
    tc->structs = NULL; tc->struct_count = 0; tc->struct_capacity = 0;
    tc->owned_function_types = NULL; tc->owned_function_type_count = 0; tc->owned_function_type_capacity = 0;
    tc->checked_modules = NULL; tc->checked_module_count = 0; tc->checked_module_capacity = 0;
    return tc;
}

static void type_checker_track_function_type(TypeChecker *tc, TypeNode *type) {
    if (!tc || !type) return;
    if (tc->owned_function_type_count >= tc->owned_function_type_capacity) {
        tc->owned_function_type_capacity = tc->owned_function_type_capacity ? tc->owned_function_type_capacity * 2 : 16;
        tc->owned_function_types = ocl_realloc(tc->owned_function_types,
                                               tc->owned_function_type_capacity * sizeof(TypeNode *));
    }
    tc->owned_function_types[tc->owned_function_type_count++] = type;
}

static void type_checker_free_function_type(TypeNode *type) {
    if (!type) return;
    ocl_free(type->param_types);
    ocl_free(type);
}

void type_checker_free(TypeChecker *tc) {
    if (!tc) return;
    symbol_table_free(tc->symbol_table);
    for (size_t i = 0; i < tc->struct_count; i++) {
        ocl_free(tc->structs[i].name);
        ocl_free(tc->structs[i].type.struct_name);
    }
    ocl_free(tc->structs);
    for (size_t i = 0; i < tc->owned_function_type_count; i++)
        type_checker_free_function_type(tc->owned_function_types[i]);
    ocl_free(tc->owned_function_types);
    for (size_t i = 0; i < tc->checked_module_count; i++)
        ocl_free(tc->checked_modules[i]);
    ocl_free(tc->checked_modules);
    ocl_free(tc);
}

static void check_node(TypeChecker *tc, ASTNode *node);
static TypeNode *check_expr(TypeChecker *tc, ExprNode *expr);
static void register_stdlib_symbols(TypeChecker *tc);

static bool accepts_argc(int param_count, size_t argc) {
    if (OCL_ARGS_VARIADIC(param_count))
        return (int)argc >= OCL_ARGS_MIN(param_count);
    return param_count == (int)argc;
}

static TypeNode *integer_literal_type(void) {
    static TypeNode literal = {
        TYPE_INT,
        INTEGER_KIND_LITERAL,
        NULL,
        NULL,
        NULL,
        0,
        NULL
    };
    return &literal;
}

static TypeNode *builtin_type(BuiltinType bt) {
    static TypeNode types[TYPE_UNKNOWN + 1];
    static bool initialised = false;
    if (!initialised) {
        for (int i = 0; i <= (int)TYPE_UNKNOWN; i++) {
            types[i].type         = (BuiltinType)i;
            types[i].integer_kind = (i == TYPE_INT) ? INTEGER_KIND_GENERIC_INT : INTEGER_KIND_NONE;
            types[i].element_type = NULL;
            types[i].struct_name  = NULL;
            types[i].param_types  = NULL;
            types[i].param_count  = 0;
            types[i].return_type  = NULL;
        }
        initialised = true;
    }
    if ((int)bt < 0 || (int)bt > (int)TYPE_UNKNOWN) bt = TYPE_UNKNOWN;
    return &types[(int)bt];
}

static TypeNode *make_function_type(TypeNode *return_type, TypeNode **param_types, size_t param_count) {
    TypeNode *type = ast_create_type(TYPE_FUNCTION);
    type->return_type = return_type;
    type->param_types = param_types;
    type->param_count = param_count;
    return type;
}

static TypeNode *make_tracked_function_type(TypeChecker *tc, TypeNode *return_type,
                                            TypeNode **param_types, size_t param_count) {
    TypeNode *type = make_function_type(return_type, param_types, param_count);
    type_checker_track_function_type(tc, type);
    return type;
}

static TypeNode *make_builtin_function_type(TypeChecker *tc, BuiltinType return_type, int param_count) {
    size_t arity = (size_t)(param_count < 0 ? OCL_ARGS_MIN(param_count) : param_count);
    TypeNode **param_types = arity > 0 ? ocl_malloc(arity * sizeof(TypeNode *)) : NULL;
    for (size_t i = 0; i < arity; i++)
        param_types[i] = builtin_type(TYPE_UNKNOWN);
    return make_tracked_function_type(tc, builtin_type(return_type), param_types, arity);
}

static void tc_error(TypeChecker *tc, SourceLocation loc, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (tc->errors) error_add(tc->errors, ERRK_TYPE, ERROR_TYPE_CHECKER, loc, "%s", buf);
    else fprintf(stderr, "TYPE ERROR [%d:%d]: %s\n", loc.line, loc.column, buf);
    tc->error_count++;
}

static bool type_matches(TypeNode *expected, TypeNode *actual);

static bool type_is_integer(const TypeNode *type) {
    return ast_type_is_integer(type);
}

static bool type_is_integer_literal(const TypeNode *type) {
    return type_is_integer(type) && type->integer_kind == INTEGER_KIND_LITERAL;
}

static bool type_is_numeric(const TypeNode *type) {
    return type && (type_is_integer(type) || type->type == TYPE_FLOAT);
}

static int integer_rank(const TypeNode *type) {
    if (!type_is_integer(type))
        return -1;

    switch (type->integer_kind) {
        case INTEGER_KIND_LITERAL:     return 0;
        case INTEGER_KIND_ICHAR:
        case INTEGER_KIND_CHAR:        return 1;
        case INTEGER_KIND_SHORT:
        case INTEGER_KIND_USHORT:      return 2;
        case INTEGER_KIND_INT:
        case INTEGER_KIND_UINT:        return 3;
        case INTEGER_KIND_GENERIC_INT:
        case INTEGER_KIND_LONG:
        case INTEGER_KIND_ULONG:
        case INTEGER_KIND_IPTR:
        case INTEGER_KIND_ISZ:
        case INTEGER_KIND_UPTR:
        case INTEGER_KIND_USZ:         return 4;
        case INTEGER_KIND_INT128:
        case INTEGER_KIND_UINT128:     return 5;
        case INTEGER_KIND_NONE:        return -1;
    }

    return -1;
}

static TypeNode *promote_integer_types(TypeNode *left, TypeNode *right) {
    if (type_is_integer_literal(left) && type_is_integer(right))
        return right;
    if (type_is_integer(left) && type_is_integer_literal(right))
        return left;
    if (type_is_integer_literal(left) && type_is_integer_literal(right))
        return builtin_type(TYPE_INT);
    if (!type_is_integer(left) || !type_is_integer(right))
        return builtin_type(TYPE_INT);
    return integer_rank(left) >= integer_rank(right) ? left : right;
}

static bool integer_literal_fits(TypeNode *expected, int64_t value) {
    const IntegerTypeInfo *info;

    if (!type_is_integer(expected))
        return false;
    if (expected->integer_kind == INTEGER_KIND_LITERAL)
        return true;

    info = ast_integer_type_info(expected->integer_kind);
    if (!info)
        return false;

    if (info->is_signed) {
        int64_t min_value;
        int64_t max_value;

        if (info->bits == 0 || info->bits >= 64)
            return true;

        min_value = -(1LL << (info->bits - 1));
        max_value = (1LL << (info->bits - 1)) - 1;
        return value >= min_value && value <= max_value;
    }

    if (value < 0)
        return false;
    if (info->bits == 0 || info->bits >= 63)
        return true;
    return (uint64_t)value <= ((1ULL << info->bits) - 1ULL);
}

static bool type_accepts_expr(TypeNode *expected, ExprNode *expr, TypeNode *actual) {
    LiteralNode *literal;

    if (!expected || !actual)
        return true;
    if (expr && expr->base.type == AST_LITERAL && type_is_integer(expected)) {
        literal = (LiteralNode *)expr;
        if (literal->value.type == VALUE_INT)
            return integer_literal_fits(expected, literal->value.data.int_val);
        if (literal->value.type == VALUE_CHAR)
            return integer_literal_fits(expected, (unsigned char)literal->value.data.char_val);
    }
    if (type_matches(expected, actual))
        return true;
    if (!expr || expr->base.type != AST_LITERAL || !type_is_integer(expected))
        return false;

    literal = (LiteralNode *)expr;
    return false;
}

static bool type_matches(TypeNode *expected, TypeNode *actual) {
    if (!expected || !actual) return true;
    if (expected->type == TYPE_VOID) return true;
    if (expected->type == TYPE_UNKNOWN || actual->type == TYPE_UNKNOWN) return true;
    if (type_is_integer(expected) && type_is_integer(actual)) return true;
    if (expected->type != actual->type) return false;
    if (expected->type == TYPE_FUNCTION) {
        if (expected->param_count != actual->param_count)
            return false;
        for (size_t i = 0; i < expected->param_count; i++) {
            if (!type_matches(expected->param_types[i], actual->param_types[i]))
                return false;
        }
        return type_matches(expected->return_type, actual->return_type);
    }
    if (expected->type == TYPE_STRUCT &&
        expected->struct_name && actual->struct_name &&
        strcmp(expected->struct_name, actual->struct_name) != 0)
        return false;
    return true;
}

static TypeNode *merge_conditional_types(TypeChecker *tc, TypeNode *left,
                                         TypeNode *right, SourceLocation loc) {
    if (!left) return right ? right : builtin_type(TYPE_UNKNOWN);
    if (!right) return left;
    if (left->type == TYPE_UNKNOWN) return right;
    if (right->type == TYPE_UNKNOWN) return left;

    if (type_is_numeric(left) && type_is_numeric(right)) {
        if (left->type == TYPE_FLOAT || right->type == TYPE_FLOAT)
            return builtin_type(TYPE_FLOAT);
        return promote_integer_types(left, right);
    }

    if (left->type == right->type) {
        if (left->type != TYPE_STRUCT)
            return left;
        if ((!left->struct_name && !right->struct_name) ||
            (left->struct_name && right->struct_name &&
             strcmp(left->struct_name, right->struct_name) == 0))
            return left;
    }

    tc_error(tc, loc, "Ternary branches must return compatible types");
    return builtin_type(TYPE_UNKNOWN);
}

static StructSymbol *find_struct_symbol(TypeChecker *tc, const char *name) {
    if (!tc || !name) return NULL;
    for (size_t i = 0; i < tc->struct_count; i++)
        if (strcmp(tc->structs[i].name, name) == 0) return &tc->structs[i];
    return NULL;
}

static ParamNode *find_struct_field(StructSymbol *sym, const char *name) {
    if (!sym || !name) return NULL;
    for (size_t i = 0; i < sym->field_count; i++) {
        if (sym->fields[i] && sym->fields[i]->name &&
            strcmp(sym->fields[i]->name, name) == 0)
            return sym->fields[i];
    }
    return NULL;
}

static bool has_checked_module(TypeChecker *tc, const char *path) {
    if (!tc || !path) return false;
    for (size_t i = 0; i < tc->checked_module_count; i++)
        if (strcmp(tc->checked_modules[i], path) == 0) return true;
    return false;
}

static void mark_checked_module(TypeChecker *tc, const char *path) {
    if (!tc || !path || has_checked_module(tc, path)) return;
    if (tc->checked_module_count >= tc->checked_module_capacity) {
        tc->checked_module_capacity = tc->checked_module_capacity ? tc->checked_module_capacity * 2 : 8;
        tc->checked_modules = ocl_realloc(tc->checked_modules,
                                          tc->checked_module_capacity * sizeof(char *));
    }
    tc->checked_modules[tc->checked_module_count++] = ocl_strdup(path);
}

static void register_struct_decl(TypeChecker *tc, StructDeclNode *decl) {
    if (!tc || !decl || !decl->name) return;

    StructSymbol *existing = find_struct_symbol(tc, decl->name);
    if (existing) return;

    if (tc->struct_count >= tc->struct_capacity) {
        tc->struct_capacity = tc->struct_capacity ? tc->struct_capacity * 2 : 8;
        tc->structs = ocl_realloc(tc->structs, tc->struct_capacity * sizeof(StructSymbol));
    }

    StructSymbol *sym = &tc->structs[tc->struct_count++];
    sym->name = ocl_strdup(decl->name);
    sym->fields = decl->fields;
    sym->field_count = decl->field_count;
    sym->type.type = TYPE_STRUCT;
    sym->type.integer_kind = INTEGER_KIND_NONE;
    sym->type.element_type = NULL;
    sym->type.struct_name = ocl_strdup(decl->name);
    sym->type.param_types = NULL;
    sym->type.param_count = 0;
    sym->type.return_type = NULL;
}

static void predeclare_program(TypeChecker *tc, ProgramNode *program) {
    if (!tc || !program) return;

    for (size_t i = 0; i < program->import_count; i++)
        predeclare_program(tc, program->imports[i]);

    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_STRUCT_DECL) {
            register_struct_decl(tc, (StructDeclNode *)n);
        } else if (n->type == AST_FUNC_DECL) {
            FuncDeclNode *f = (FuncDeclNode *)n;
            if (symbol_table_has_in_current_scope(tc->symbol_table, f->name)) continue;
            TypeNode **ptypes = NULL;
            if (f->param_count > 0) {
                ptypes = ocl_malloc(f->param_count * sizeof(TypeNode *));
                for (size_t j = 0; j < f->param_count; j++)
                    ptypes[j] = f->params[j]->type;
            }
            symbol_table_insert_func(tc->symbol_table,
                                     f->name,
                                     make_tracked_function_type(tc, f->return_type, ptypes, f->param_count),
                                     ptypes,
                                     (int)f->param_count);
        } else if (n->type == AST_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n;
            if (!symbol_table_has_in_current_scope(tc->symbol_table, v->name))
                symbol_table_insert(tc->symbol_table, v->name, v->type, false, false);
        } else if (n->type == AST_DECLARE) {
            DeclareNode *d = (DeclareNode *)n;
            if (!symbol_table_has_in_current_scope(tc->symbol_table, d->name))
                symbol_table_insert(tc->symbol_table, d->name, d->type, false, false);
        }
    }
}

static void check_program(TypeChecker *tc, ProgramNode *program) {
    if (!tc || !program) return;
    if (program->module_path && has_checked_module(tc, program->module_path))
        return;

    if (program->module_path)
        mark_checked_module(tc, program->module_path);

    for (size_t i = 0; i < program->import_count; i++)
        check_program(tc, program->imports[i]);

    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n;
            if (v->initializer) check_expr(tc, v->initializer);
        } else if (n->type != AST_STRUCT_DECL) {
            check_node(tc, n);
        }
    }
}

static TypeNode *check_expr(TypeChecker *tc, ExprNode *expr) {
    if (!expr) return builtin_type(TYPE_VOID);
    switch (expr->base.type) {
        case AST_LITERAL: {
            LiteralNode *lit = (LiteralNode *)expr;
            BuiltinType bt;
            switch (lit->value.type) {
                case VALUE_INT:    bt = TYPE_INT;    break;
                case VALUE_FLOAT:  bt = TYPE_FLOAT;  break;
                case VALUE_STRING: bt = TYPE_STRING; break;
                case VALUE_BOOL:   bt = TYPE_BOOL;   break;
                case VALUE_CHAR:   bt = TYPE_CHAR;   break;
                case VALUE_STRUCT: bt = TYPE_STRUCT; break;
                default:           bt = TYPE_UNKNOWN; break;
            }
            if (lit->value.type == VALUE_INT)
                return integer_literal_type();
            return builtin_type(bt);
        }
        case AST_IDENTIFIER: {
            IdentifierNode *id = (IdentifierNode *)expr;
            Symbol *sym = symbol_table_lookup(tc->symbol_table, id->name);
            if (!sym) { tc_error(tc, id->base.location, "Undefined variable '%s'", id->name); return builtin_type(TYPE_UNKNOWN); }
            return sym->type ? sym->type : builtin_type(TYPE_UNKNOWN);
        }
        case AST_BIN_OP: {
            BinOpNode *b = (BinOpNode *)expr;
            TypeNode *lt = check_expr(tc, b->left);
            TypeNode *rt = check_expr(tc, b->right);
            if (!strcmp(b->operator, "??"))
                return merge_conditional_types(tc, lt, rt, b->base.location);
            if (!strcmp(b->operator, "=")) {
                if (b->left && b->left->base.type == AST_IDENTIFIER) {
                    IdentifierNode *id = (IdentifierNode *)b->left;
                    Symbol *sym = symbol_table_lookup(tc->symbol_table, id->name);
                    if (sym && sym->type && !type_accepts_expr(sym->type, b->right, rt))
                        tc_error(tc, b->base.location, "Cannot assign %s to '%s' of type %s",
                                 ast_type_name(rt), id->name, ast_type_name(sym->type));
                    return sym && sym->type ? sym->type : rt;
                } else if (b->left && b->left->base.type == AST_FIELD_ACCESS) {
                    if (!type_accepts_expr(lt, b->right, rt)) {
                        FieldAccessNode *fa = (FieldAccessNode *)b->left;
                        tc_error(tc, b->base.location, "Cannot assign %s to field '%s' of type %s",
                                 ast_type_name(rt), fa->field_name, ast_type_name(lt));
                    }
                    return lt;
                }
                return rt;
            }
            if (!strcmp(b->operator,"==")||!strcmp(b->operator,"!=")||
                !strcmp(b->operator,"<")||!strcmp(b->operator,"<=")||
                !strcmp(b->operator,">")||!strcmp(b->operator,">=")||
                !strcmp(b->operator,"&&")||!strcmp(b->operator,"||"))
                return builtin_type(TYPE_BOOL);
            if (!strcmp(b->operator,"&") || !strcmp(b->operator,"|") ||
                !strcmp(b->operator,"^") || !strcmp(b->operator,"<<") ||
                !strcmp(b->operator,">>"))
                return promote_integer_types(lt, rt);
            if (lt->type == TYPE_FLOAT || rt->type == TYPE_FLOAT) return builtin_type(TYPE_FLOAT);
            if (lt->type == TYPE_STRING && !strcmp(b->operator, "+")) return builtin_type(TYPE_STRING);
            if (type_is_integer(lt) && type_is_integer(rt))
                return promote_integer_types(lt, rt);
            return lt;
        }
        case AST_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode *)expr;
            if (!strcmp(u->operator, "!")) return builtin_type(TYPE_BOOL);
            if (!strcmp(u->operator, "~")) return check_expr(tc, u->operand);
            return check_expr(tc, u->operand);
        }
        case AST_TERNARY: {
            TernaryNode *t = (TernaryNode *)expr;
            check_expr(tc, t->condition);
            TypeNode *true_type = check_expr(tc, t->true_expr);
            TypeNode *false_type = check_expr(tc, t->false_expr);
            return merge_conditional_types(tc, true_type, false_type, t->base.location);
        }
        case AST_CALL: {
            CallNode *c = (CallNode *)expr;
            TypeNode *callee_type = check_expr(tc, c->callee);
            Symbol *callee_symbol = NULL;
            if (c->callee && c->callee->base.type == AST_IDENTIFIER) {
                IdentifierNode *id = (IdentifierNode *)c->callee;
                callee_symbol = symbol_table_lookup(tc->symbol_table, id->name);
            }
            int expected_argc = (callee_symbol && callee_symbol->is_function)
                                ? callee_symbol->param_count
                                : (int)callee_type->param_count;
            if (!callee_type || callee_type->type != TYPE_FUNCTION) {
                tc_error(tc, c->base.location, "Call target must be a function value");
                for (size_t i = 0; i < c->argument_count; i++) check_expr(tc, c->arguments[i]);
                return builtin_type(TYPE_UNKNOWN);
            }
            if (!accepts_argc(expected_argc, c->argument_count)) {
                tc_error(tc, c->base.location, "Function value expects %llu arguments, got %llu",
                         (unsigned long long)(expected_argc < 0 ? OCL_ARGS_MIN(expected_argc) : expected_argc),
                         (unsigned long long)c->argument_count);
            }
            for (size_t i = 0; i < c->argument_count; i++) {
                TypeNode *arg_type = check_expr(tc, c->arguments[i]);
                if (i < callee_type->param_count &&
                    !type_accepts_expr(callee_type->param_types[i], c->arguments[i], arg_type)) {
                    tc_error(tc, c->base.location, "Argument %llu expects %s, got %s",
                             (unsigned long long)(i + 1),
                             ast_type_name(callee_type->param_types[i]),
                             ast_type_name(arg_type));
                }
            }
            return callee_type->return_type ? callee_type->return_type : builtin_type(TYPE_VOID);
        }
        case AST_ARRAY_LITERAL: {
            ArrayLiteralNode *al = (ArrayLiteralNode *)expr;
            for (size_t i = 0; i < al->element_count; i++) check_expr(tc, al->elements[i]);
            return builtin_type(TYPE_ARRAY);
        }
        case AST_INDEX_ACCESS: {
            IndexAccessNode *ia = (IndexAccessNode *)expr;
            check_expr(tc, ia->array_expr);
            check_expr(tc, ia->index_expr);
            return builtin_type(TYPE_UNKNOWN); /* element type not tracked yet */
        }
        case AST_STRUCT_LITERAL: {
            StructLiteralNode *sl = (StructLiteralNode *)expr;
            StructSymbol *sym = find_struct_symbol(tc, sl->struct_name);
            if (!sym) {
                tc_error(tc, sl->base.location, "Unknown struct '%s'", sl->struct_name);
                for (size_t i = 0; i < sl->field_count; i++)
                    check_expr(tc, sl->field_values[i]);
                return builtin_type(TYPE_STRUCT);
            }
            for (size_t i = 0; i < sl->field_count; i++) {
                ParamNode *field = find_struct_field(sym, sl->field_names[i]);
                TypeNode *value_type = check_expr(tc, sl->field_values[i]);
                if (!field) {
                    tc_error(tc, sl->base.location, "Struct '%s' has no field '%s'",
                             sl->struct_name, sl->field_names[i]);
                    continue;
                }
                if (!type_accepts_expr(field->type, sl->field_values[i], value_type))
                    tc_error(tc, sl->base.location, "Field '%s' expects %s, got %s",
                             sl->field_names[i], ast_type_name(field->type), ast_type_name(value_type));
            }
            return &sym->type;
        }
        case AST_FIELD_ACCESS: {
            FieldAccessNode *fa = (FieldAccessNode *)expr;
            TypeNode *object_type = check_expr(tc, fa->object);
            if (object_type->type == TYPE_UNKNOWN)
                return builtin_type(TYPE_UNKNOWN);
            if (object_type->type != TYPE_STRUCT || !object_type->struct_name) {
                tc_error(tc, fa->base.location, "Field access requires a struct value");
                return builtin_type(TYPE_UNKNOWN);
            }
            StructSymbol *sym = find_struct_symbol(tc, object_type->struct_name);
            if (!sym) {
                tc_error(tc, fa->base.location, "Unknown struct '%s'", object_type->struct_name);
                return builtin_type(TYPE_UNKNOWN);
            }
            ParamNode *field = find_struct_field(sym, fa->field_name);
            if (!field) {
                tc_error(tc, fa->base.location, "Struct '%s' has no field '%s'",
                         sym->name, fa->field_name);
                return builtin_type(TYPE_UNKNOWN);
            }
            return field->type ? field->type : builtin_type(TYPE_UNKNOWN);
        }
        case AST_FUNC_EXPR: {
            FuncExprNode *fn = (FuncExprNode *)expr;
            TypeNode **param_types = NULL;
            if (fn->param_count > 0) {
                param_types = ocl_malloc(fn->param_count * sizeof(TypeNode *));
                for (size_t i = 0; i < fn->param_count; i++)
                    param_types[i] = fn->params[i]->type;
            }

            symbol_table_enter_scope(tc->symbol_table);
            TypeNode *saved_ret = tc->current_function_return_type;
            tc->current_function_return_type = fn->return_type;
            for (size_t i = 0; i < fn->param_count; i++)
                symbol_table_insert(tc->symbol_table, fn->params[i]->name, fn->params[i]->type, false, true);
            if (fn->body) {
                for (size_t i = 0; i < fn->body->statement_count; i++)
                    check_node(tc, fn->body->statements[i]);
            }
            tc->current_function_return_type = saved_ret;
            symbol_table_exit_scope(tc->symbol_table);

            return make_tracked_function_type(tc, fn->return_type, param_types, fn->param_count);
        }
        default: return builtin_type(TYPE_UNKNOWN);
    }
}

static void check_node(TypeChecker *tc, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_IMPORT:
        case AST_STRUCT_DECL:
            break;

        case AST_DECLARE: {
            DeclareNode *d = (DeclareNode *)node;
            if (!symbol_table_has_in_current_scope(tc->symbol_table, d->name))
                symbol_table_insert(tc->symbol_table, d->name, d->type, false, false);
            break;
        }

        case AST_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode *)node;
            if (symbol_table_has_in_current_scope(tc->symbol_table, v->name))
                tc_error(tc, v->base.location, "Variable '%s' already declared in this scope", v->name);
            TypeNode *init_type = NULL;
            if (v->initializer) init_type = check_expr(tc, v->initializer);
            TypeNode *decl_type = v->type ? v->type : builtin_type(TYPE_UNKNOWN);
            if (decl_type->type == TYPE_UNKNOWN && init_type) {
                decl_type->type = init_type->type;
                decl_type->integer_kind = init_type->integer_kind;
                if (init_type->struct_name) {
                    ocl_free(decl_type->struct_name);
                    decl_type->struct_name = ocl_strdup(init_type->struct_name);
                }
            } else if (init_type && !type_accepts_expr(decl_type, v->initializer, init_type)) {
                tc_error(tc, v->base.location, "Initializer for '%s' expects %s, got %s",
                         v->name, ast_type_name(decl_type), ast_type_name(init_type));
            }
            symbol_table_insert(tc->symbol_table, v->name, decl_type, false, false);
            break;
        }

        case AST_FUNC_DECL: {
            /*
             * FIX: In pass-1 of type_checker_check(), all top-level function
             * signatures are already registered in the symbol table.  Calling
             * symbol_table_insert_func here (pass-2) would add a second,
             * duplicate entry for the same name, wasting memory and potentially
             * shadowing the first entry in lookups.
             *
             * We therefore only open a new scope, register the parameters, and
             * recurse into the body — we do NOT re-register the function itself.
             */
            FuncDeclNode *f = (FuncDeclNode *)node;
            symbol_table_enter_scope(tc->symbol_table);
            TypeNode *saved_ret = tc->current_function_return_type;
            tc->current_function_return_type = f->return_type;
            for (size_t i = 0; i < f->param_count; i++)
                symbol_table_insert(tc->symbol_table, f->params[i]->name, f->params[i]->type, false, true);
            if (f->body)
                for (size_t i = 0; i < f->body->statement_count; i++)
                    check_node(tc, f->body->statements[i]);
            tc->current_function_return_type = saved_ret;
            symbol_table_exit_scope(tc->symbol_table);
            break;
        }

        case AST_BLOCK: {
            BlockNode *b = (BlockNode *)node;
            symbol_table_enter_scope(tc->symbol_table);
            for (size_t i = 0; i < b->statement_count; i++) check_node(tc, b->statements[i]);
            symbol_table_exit_scope(tc->symbol_table);
            break;
        }

        case AST_IF_STMT: {
            /* Walk the if / else-if / else chain */
            ASTNode *cur = node;
            while (cur && cur->type == AST_IF_STMT) {
                IfStmtNode *s = (IfStmtNode *)cur;
                check_expr(tc, s->condition);
                if (s->then_block) check_node(tc, (ASTNode *)s->then_block);
                cur = s->else_next;
            }
            if (cur) check_node(tc, cur);
            break;
        }

        case AST_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            if (lp->condition) check_expr(tc, lp->condition);
            /* Emit body through AST_BLOCK handler. It opens and closes its own scope. */
            if (lp->body)      check_node(tc, (ASTNode *)lp->body);
            break;
        }

        case AST_DO_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            if (lp->body)      check_node(tc, (ASTNode *)lp->body);
            if (lp->condition) check_expr(tc, lp->condition);
            break;
        }

        case AST_FOR_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            /* Open a scope for the init variable (e.g. "int i = 0"). */
            symbol_table_enter_scope(tc->symbol_table);
            if (lp->init)      check_node(tc, lp->init);
            if (lp->condition) check_expr(tc, lp->condition);
            if (lp->increment) check_node(tc, lp->increment);
            /* Body is checked through AST_BLOCK, which opens its own inner scope.
               This mirrors the codegen change and means body-local variables are
               properly scoped one level deeper than the init variable. */
            if (lp->body)      check_node(tc, (ASTNode *)lp->body);
            symbol_table_exit_scope(tc->symbol_table);
            break;
        }

        case AST_RETURN: {
            ReturnNode *r = (ReturnNode *)node;
            TypeNode *value_type = check_expr(tc, r->value);
            if (tc->current_function_return_type &&
                !type_accepts_expr(tc->current_function_return_type, r->value, value_type))
                tc_error(tc, r->base.location, "Return value expects %s, got %s",
                         ast_type_name(tc->current_function_return_type), ast_type_name(value_type));
            break;
        }
        case AST_BREAK: case AST_CONTINUE: break;
        default: check_expr(tc, (ExprNode *)node); break;
    }
}

bool type_checker_check(TypeChecker *tc, ProgramNode *program) {
    if (!tc || !program) return false;

    register_stdlib_symbols(tc);
    predeclare_program(tc, program);
    check_program(tc, program);
    return tc->error_count == 0;
}

int type_checker_get_error_count(TypeChecker *tc) { return tc ? tc->error_count : 0; }

static void register_stdlib_symbols(TypeChecker *tc) {
    size_t count = 0;
    const StdlibEntry *table;
    static const struct {
        const char *name;
        BuiltinType return_type;
        int param_count;
    } bitwise_builtins[] = {
        { "bitLogicalShiftRight", TYPE_INT,  2 },
        { "bitRotateLeft",        TYPE_INT,  2 },
        { "bitRotateRight",       TYPE_INT,  2 },
        { "bitPopcount",          TYPE_INT,  1 },
        { "bitCountLeadingZeros", TYPE_INT,  1 },
        { "bitCountTrailingZeros",TYPE_INT,  1 },
        { "bitTest",              TYPE_BOOL, 2 },
        { "bitSet",               TYPE_INT,  2 },
        { "bitClear",             TYPE_INT,  2 },
        { "bitToggle",            TYPE_INT,  2 },
        { "bitNand",              TYPE_INT,  2 },
        { "bitNor",               TYPE_INT,  2 },
        { "bitXnor",              TYPE_INT,  2 },
    };

    if (!tc || !tc->symbol_table) return;

    table = stdlib_get_table(&count);
    for (size_t i = 0; i < count; i++) {
        const StdlibEntry *entry = &table[i];
        if (symbol_table_has_in_current_scope(tc->symbol_table, entry->name)) continue;
        symbol_table_insert_func(tc->symbol_table,
                                 entry->name,
                                 make_builtin_function_type(tc, entry->return_type, entry->param_count),
                                 NULL,
                                 entry->param_count);
    }

    for (size_t i = 0; i < sizeof(bitwise_builtins) / sizeof(bitwise_builtins[0]); i++) {
        if (symbol_table_has_in_current_scope(tc->symbol_table, bitwise_builtins[i].name)) continue;
        symbol_table_insert_func(tc->symbol_table,
                                 bitwise_builtins[i].name,
                                 make_builtin_function_type(tc, bitwise_builtins[i].return_type, bitwise_builtins[i].param_count),
                                 NULL,
                                 bitwise_builtins[i].param_count);
    }
}
