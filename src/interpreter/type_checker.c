
#include "type_checker.h"
#include "ocl_stdlib.h"
#include "common.h"
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
        ocl_free(s->name); ocl_free(s->param_types); ocl_free(s);
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
            ocl_free(s->name); ocl_free(s->param_types); ocl_free(s);
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

void symbol_table_insert_func(SymbolTable *t, const char *name, TypeNode *ret_type, TypeNode **param_types, size_t param_count) {
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
    return tc;
}

void type_checker_free(TypeChecker *tc) {
    if (!tc) return; symbol_table_free(tc->symbol_table); ocl_free(tc);
}

static void check_node(TypeChecker *tc, ASTNode *node);
static TypeNode *check_expr(TypeChecker *tc, ExprNode *expr);

static TypeNode *builtin_type(BuiltinType bt) {
    static TypeNode types[TYPE_UNKNOWN + 1];
    static bool initialised = false;
    if (!initialised) {
        for (int i = 0; i <= (int)TYPE_UNKNOWN; i++) {
            types[i].type = (BuiltinType)i; types[i].bit_width = 0;
            types[i].element_type = NULL; types[i].is_array = false;
        }
        initialised = true;
    }
    if ((int)bt < 0 || (int)bt > (int)TYPE_UNKNOWN) bt = TYPE_UNKNOWN;
    return &types[(int)bt];
}

static void tc_error(TypeChecker *tc, SourceLocation loc, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (tc->errors) error_add(tc->errors, ERROR_TYPE_CHECKER, loc, "%s", buf);
    else fprintf(stderr, "TYPE ERROR [%d:%d]: %s\n", loc.line, loc.column, buf);
    tc->error_count++;
}

static bool is_builtin_function(const char *name) {
    if (!strcmp(name, "print") || !strcmp(name, "printf")) return true;
    return stdlib_lookup_by_name(name) != NULL;
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
                default:           bt = TYPE_UNKNOWN; break;
            }
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
            if (!strcmp(b->operator, "=")) return rt;
            if (!strcmp(b->operator,"==")||!strcmp(b->operator,"!=")||
                !strcmp(b->operator,"<")||!strcmp(b->operator,"<=")||
                !strcmp(b->operator,">")||!strcmp(b->operator,">=")||
                !strcmp(b->operator,"&&")||!strcmp(b->operator,"||"))
                return builtin_type(TYPE_BOOL);
            if (lt->type == TYPE_FLOAT || rt->type == TYPE_FLOAT) return builtin_type(TYPE_FLOAT);
            if (lt->type == TYPE_STRING && !strcmp(b->operator, "+")) return builtin_type(TYPE_STRING);
            return lt;
        }
        case AST_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode *)expr;
            if (!strcmp(u->operator, "!")) return builtin_type(TYPE_BOOL);
            return check_expr(tc, u->operand);
        }
        case AST_CALL: {
            CallNode *c = (CallNode *)expr;
            if (is_builtin_function(c->function_name)) {
                for (size_t i = 0; i < c->argument_count; i++) check_expr(tc, c->arguments[i]);
                return builtin_type(TYPE_UNKNOWN);
            }
            Symbol *sym = symbol_table_lookup(tc->symbol_table, c->function_name);
            if (!sym) { tc_error(tc, c->base.location, "Undefined function '%s'", c->function_name); return builtin_type(TYPE_UNKNOWN); }
            if (sym->is_function && sym->param_count != c->argument_count)
                tc_error(tc, c->base.location, "Function '%s' expects %zu arguments, got %zu", c->function_name, sym->param_count, c->argument_count);
            for (size_t i = 0; i < c->argument_count; i++) check_expr(tc, c->arguments[i]);
            return sym->type ? sym->type : builtin_type(TYPE_VOID);
        }
        default: return builtin_type(TYPE_UNKNOWN);
    }
}

static void check_node(TypeChecker *tc, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_IMPORT: case AST_DECLARE: break;
        case AST_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode *)node;
            if (symbol_table_has_in_current_scope(tc->symbol_table, v->name))
                tc_error(tc, v->base.location, "Variable '%s' already declared in this scope", v->name);
            TypeNode *init_type = NULL;
            if (v->initializer) init_type = check_expr(tc, v->initializer);
            TypeNode *decl_type = v->type;
            if (decl_type && decl_type->type == TYPE_UNKNOWN && init_type) decl_type->type = init_type->type;
            symbol_table_insert(tc->symbol_table, v->name, decl_type, false, false);
            break;
        }
        case AST_FUNC_DECL: {
            FuncDeclNode *f = (FuncDeclNode *)node;
            TypeNode **ptypes = NULL;
            if (f->param_count > 0) {
                ptypes = ocl_malloc(f->param_count * sizeof(TypeNode *));
                for (size_t i = 0; i < f->param_count; i++) ptypes[i] = f->params[i]->type;
            }
            symbol_table_insert_func(tc->symbol_table, f->name, f->return_type, ptypes, f->param_count);
            symbol_table_enter_scope(tc->symbol_table);
            TypeNode *saved_ret = tc->current_function_return_type;
            tc->current_function_return_type = f->return_type;
            for (size_t i = 0; i < f->param_count; i++)
                symbol_table_insert(tc->symbol_table, f->params[i]->name, f->params[i]->type, false, true);
            if (f->body) for (size_t i = 0; i < f->body->statement_count; i++) check_node(tc, f->body->statements[i]);
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
            IfStmtNode *s = (IfStmtNode *)node;
            check_expr(tc, s->condition);
            if (s->then_block) check_node(tc, (ASTNode *)s->then_block);
            if (s->else_block) check_node(tc, (ASTNode *)s->else_block);
            break;
        }
        case AST_FOR_LOOP: case AST_WHILE_LOOP: {
            LoopNode *lp = (LoopNode *)node;
            symbol_table_enter_scope(tc->symbol_table);
            if (lp->init) check_node(tc, lp->init);
            if (lp->condition) check_expr(tc, lp->condition);
            if (lp->increment) check_node(tc, lp->increment);
            if (lp->body) for (size_t i = 0; i < lp->body->statement_count; i++) check_node(tc, lp->body->statements[i]);
            symbol_table_exit_scope(tc->symbol_table);
            break;
        }
        case AST_RETURN: { ReturnNode *r = (ReturnNode *)node; check_expr(tc, r->value); break; }
        case AST_BREAK: case AST_CONTINUE: break;
        default: check_expr(tc, (ExprNode *)node); break;
    }
}

bool type_checker_check(TypeChecker *tc, ProgramNode *program) {
    if (!tc || !program) return false;
    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_FUNC_DECL) {
            FuncDeclNode *f = (FuncDeclNode *)n;
            TypeNode **ptypes = NULL;
            if (f->param_count > 0) {
                ptypes = ocl_malloc(f->param_count * sizeof(TypeNode *));
                for (size_t j = 0; j < f->param_count; j++) ptypes[j] = f->params[j]->type;
            }
            symbol_table_insert_func(tc->symbol_table, f->name, f->return_type, ptypes, f->param_count);
        } else if (n->type == AST_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n;
            symbol_table_insert(tc->symbol_table, v->name, v->type, false, false);
        }
    }
    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_VAR_DECL) { VarDeclNode *v = (VarDeclNode *)n; if (v->initializer) check_expr(tc, v->initializer); }
        else check_node(tc, n);
    }
    return tc->error_count == 0;
}

int type_checker_get_error_count(TypeChecker *tc) { return tc ? tc->error_count : 0; }
