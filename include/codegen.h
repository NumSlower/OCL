#ifndef OCL_CODEGEN_H
#define OCL_CODEGEN_H

#include "ast.h"
#include "bytecode.h"
#include "errors.h"

/* Variable mapping for code generation */
typedef struct {
    char *name;
    int index;
} VariableMapping;

/* Built-in function tracking */
typedef struct {
    char *name;
    int id;  /* Special ID for built-in functions */
} BuiltinFunction;

/* Code generator */
typedef struct {
    Bytecode *bytecode;
    ErrorCollector *errors;
    int local_count;
    int scope_level;
    
    /* Variable tracking */
    VariableMapping *variables;
    size_t variable_count;
    size_t variable_capacity;
    
    /* Built-in function tracking */
    BuiltinFunction *builtins;
    size_t builtin_count;
    size_t builtin_capacity;
} CodeGenerator;

/* Code generator functions */
CodeGenerator *codegen_create(ErrorCollector *errors);
void codegen_free(CodeGenerator *gen);
bool codegen_generate(CodeGenerator *gen, ProgramNode *program, Bytecode *output);

#endif /* OCL_CODEGEN_H */
