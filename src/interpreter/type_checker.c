#include "type_checker.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

SymbolTable *symbol_table_create(void) {
    SymbolTable *table = ocl_malloc(sizeof(SymbolTable));
    table->symbols = NULL;
    table->symbol_count = 0;
    table->capacity = 0;
    table->current_scope_level = 0;
    return table;
}

void symbol_table_free(SymbolTable *table) {
    if (!table) return;
    
    for (size_t i = 0; i < table->symbol_count; i++) {
        ocl_free(table->symbols[i]->name);
        ocl_free(table->symbols[i]->type);
        ocl_free(table->symbols[i]);
    }
    ocl_free(table->symbols);
    ocl_free(table);
}

void symbol_table_enter_scope(SymbolTable *table) {
    if (table) table->current_scope_level++;
}

void symbol_table_exit_scope(SymbolTable *table) {
    if (!table) return;
    
    /* Remove all symbols in current scope, working backwards */
    size_t i = table->symbol_count;
    while (i > 0) {
        i--;
        if (table->symbols[i]->scope_level == table->current_scope_level) {
            /* Free the symbol */
            ocl_free(table->symbols[i]->name);
            ocl_free(table->symbols[i]->type);
            ocl_free(table->symbols[i]);
            
            /* Remove from array by shifting later elements forward */
            for (size_t j = i; j < table->symbol_count - 1; j++) {
                table->symbols[j] = table->symbols[j + 1];
            }
            table->symbol_count--;
            /* Don't decrement i, check same position again */
        }
    }
    
    table->current_scope_level--;
}

void symbol_table_insert(SymbolTable *table, const char *name, TypeNode *type, bool is_function, bool is_parameter) {
    if (!table) return;
    
    if (table->symbol_count >= table->capacity) {
        table->capacity = (table->capacity == 0) ? 16 : table->capacity * 2;
        table->symbols = ocl_realloc(table->symbols, table->capacity * sizeof(Symbol *));
    }
    
    Symbol *sym = ocl_malloc(sizeof(Symbol));
    sym->name = (char *)ocl_malloc(strlen(name) + 1);
    strcpy(sym->name, name);
    sym->type = type;
    sym->is_function = is_function;
    sym->is_parameter = is_parameter;
    sym->scope_level = table->current_scope_level;
    
    table->symbols[table->symbol_count++] = sym;
}

Symbol *symbol_table_lookup(SymbolTable *table, const char *name) {
    if (!table) return NULL;
    
    for (int i = table->symbol_count - 1; i >= 0; i--) {
        if (strcmp(table->symbols[i]->name, name) == 0) {
            return table->symbols[i];
        }
    }
    return NULL;
}

bool symbol_table_has_in_current_scope(SymbolTable *table, const char *name) {
    if (!table) return false;
    
    for (size_t i = 0; i < table->symbol_count; i++) {
        if (table->symbols[i]->scope_level == table->current_scope_level &&
            strcmp(table->symbols[i]->name, name) == 0) {
            return true;
        }
    }
    return false;
}

TypeChecker *type_checker_create(void) {
    TypeChecker *checker = ocl_malloc(sizeof(TypeChecker));
    checker->symbol_table = symbol_table_create();
    checker->current_function_return_type = NULL;
    checker->error_count = 0;
    return checker;
}

void type_checker_free(TypeChecker *checker) {
    if (!checker) return;
    
    symbol_table_free(checker->symbol_table);
    ocl_free(checker);
}

bool type_checker_check(TypeChecker *checker, ProgramNode *program) {
    if (!checker || !program) return false;
    
    /* TODO: Implement type checking pass */
    /* For now, return success */
    return checker->error_count == 0;
}

int type_checker_get_error_count(TypeChecker *checker) {
    return checker ? checker->error_count : 0;
}
