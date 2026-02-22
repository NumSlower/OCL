#ifndef OCL_TYPE_CHECKER_H
#define OCL_TYPE_CHECKER_H

#include "ast.h"

/* Symbol table entry */
typedef struct {
    char *name;
    TypeNode *type;
    bool is_function;
    bool is_parameter;
    int scope_level;
} Symbol;

/* Symbol table */
typedef struct {
    Symbol **symbols;
    size_t symbol_count;
    size_t capacity;
    int current_scope_level;
} SymbolTable;

/* Type checker structure */
typedef struct {
    SymbolTable *symbol_table;
    TypeNode *current_function_return_type;
    int error_count;
} TypeChecker;

/* Type checker functions */
TypeChecker *type_checker_create(void);
void type_checker_free(TypeChecker *checker);
bool type_checker_check(TypeChecker *checker, ProgramNode *program);
int type_checker_get_error_count(TypeChecker *checker);

/* Symbol table functions */
SymbolTable *symbol_table_create(void);
void symbol_table_free(SymbolTable *table);
void symbol_table_enter_scope(SymbolTable *table);
void symbol_table_exit_scope(SymbolTable *table);
void symbol_table_insert(SymbolTable *table, const char *name, TypeNode *type, bool is_function, bool is_parameter);
Symbol *symbol_table_lookup(SymbolTable *table, const char *name);
bool symbol_table_has_in_current_scope(SymbolTable *table, const char *name);

#endif /* OCL_TYPE_CHECKER_H */
