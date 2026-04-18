#include "codegen.h"
#include "common.h"
#include "ocl_stdlib.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════
   Symbol table helpers
   ══════════════════════════════════════════════════════════════════ */

static FuncEntry *current_function_entry(CodeGenerator *g) {
    if (!g || g->function_stack_top <= 0) return NULL;
    uint32_t index = g->function_stack[g->function_stack_top - 1].function_index;
    if (index >= g->bytecode->function_count) return NULL;
    return &g->bytecode->functions[index];
}

/* Look up a variable by name. Searches backwards so inner-scope declarations shadow outer ones. */
static void register_local_name(CodeGenerator *g, int slot, const char *name) {
    FuncEntry *fn = current_function_entry(g);
    if (!fn || slot < 0 || !name) return;

    if ((size_t)(slot + 1) > fn->local_name_count) {
        size_t old_count = fn->local_name_count;
        fn->local_names = ocl_realloc(fn->local_names, (size_t)(slot + 1) * sizeof(char *));
        for (size_t i = old_count; i < (size_t)(slot + 1); i++)
            fn->local_names[i] = NULL;
        fn->local_name_count = (size_t)(slot + 1);
    }

    ocl_free(fn->local_names[slot]);
    fn->local_names[slot] = ocl_strdup(name);
}

static int add_capture(CodeGenerator *g, const VarSlot *outer) {
    FuncEntry *fn = current_function_entry(g);
    if (!fn || !outer) return -1;

    for (size_t i = 0; i < fn->capture_count; i++) {
        if (!strcmp(fn->captures[i].name, outer->name))
            return (int)i;
    }

    fn->captures = ocl_realloc(fn->captures, (fn->capture_count + 1) * sizeof(FuncCapture));
    FuncCapture *capture = &fn->captures[fn->capture_count];
    capture->name = ocl_strdup(outer->name);
    capture->source = (outer->kind == VAR_SLOT_CAPTURE) ? FUNC_CAPTURE_CAPTURE : FUNC_CAPTURE_LOCAL;
    capture->slot = (uint32_t)outer->slot;
    return (int)fn->capture_count++;
}

static VarSlot *resolve_var(CodeGenerator *g, const char *name) {
    VarSlot *outer = NULL;

    for (int i = (int)g->var_count - 1; i >= 0; i--) {
        if (strcmp(g->vars[i].name, name) != 0)
            continue;
        if (g->vars[i].function_depth == g->function_stack_top)
            return &g->vars[i];
        if (!outer)
            outer = &g->vars[i];
    }

    if (!outer || g->function_stack_top <= 0)
        return outer;

    int slot = add_capture(g, outer);
    if (slot < 0)
        return outer;

    if (g->var_count >= g->var_cap) {
        g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
        g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
    }
    g->vars[g->var_count].name = ocl_strdup(name);
    g->vars[g->var_count].slot = slot;
    g->vars[g->var_count].scope_level = g->scope_level;
    g->vars[g->var_count].function_depth = g->function_stack_top;
    g->vars[g->var_count].kind = VAR_SLOT_CAPTURE;
    return &g->vars[g->var_count++];
}

static VarSlot *lookup_current_function_var(CodeGenerator *g, const char *name) {
    for (int i = (int)g->var_count - 1; i >= 0; i--) {
        if (!strcmp(g->vars[i].name, name) &&
            g->vars[i].function_depth == g->function_stack_top)
            return &g->vars[i];
    }
    return NULL;
}

/* Look up a global variable by name; returns its slot index or -1. */
static int lookup_global(CodeGenerator *g, const char *name) {
    for (size_t i = 0; i < g->global_count; i++)
        if (!strcmp(g->globals[i].name, name)) return g->globals[i].slot;
    return -1;
}

/*
 * add_local — register a new local variable in the current function frame.
 * Assigns the next available slot from the frame's local counter.
 */
static int add_local(CodeGenerator *g, const char *name) {
    int slot = g->local_stack[g->local_stack_top - 1]++;
    if (g->var_count >= g->var_cap) {
        g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
        g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
    }
    g->vars[g->var_count].name        = ocl_strdup(name);
    g->vars[g->var_count].slot        = slot;
    g->vars[g->var_count].scope_level = g->scope_level;
    g->vars[g->var_count].function_depth = g->function_stack_top;
    g->vars[g->var_count].kind = VAR_SLOT_LOCAL;
    g->var_count++;
    register_local_name(g, slot, name);
    return slot;
}

static void add_param(CodeGenerator *g, const char *name, int slot) {
    if (g->var_count >= g->var_cap) {
        g->var_cap = g->var_cap ? g->var_cap * 2 : 32;
        g->vars = ocl_realloc(g->vars, g->var_cap * sizeof(VarSlot));
    }
    g->vars[g->var_count].name = ocl_strdup(name);
    g->vars[g->var_count].slot = slot;
    g->vars[g->var_count].scope_level = g->scope_level;
    g->vars[g->var_count].function_depth = g->function_stack_top;
    g->vars[g->var_count].kind = VAR_SLOT_LOCAL;
    g->var_count++;
    register_local_name(g, slot, name);
}

/* add_global — register a new global variable and assign the next global slot. */
static int add_global(CodeGenerator *g, const char *name) {
    if (g->global_count >= g->global_cap) {
        g->global_cap = g->global_cap ? g->global_cap * 2 : 16;
        g->globals = ocl_realloc(g->globals, g->global_cap * sizeof(VarSlot));
    }
    int slot = (int)g->global_count;
    g->globals[g->global_count].name        = ocl_strdup(name);
    g->globals[g->global_count].slot        = slot;
    g->globals[g->global_count].scope_level = 0;
    g->globals[g->global_count].function_depth = 0;
    g->globals[g->global_count].kind = VAR_SLOT_LOCAL;
    g->global_count++;
    return slot;
}

/* enter_scope / exit_scope — manage lexical variable scoping. */
static void enter_scope(CodeGenerator *g) { g->scope_level++; }

static void exit_scope(CodeGenerator *g) {
    size_t write = 0;
    for (size_t i = 0; i < g->var_count; i++) {
        if (g->vars[i].scope_level < g->scope_level)
            g->vars[write++] = g->vars[i];
        else
            ocl_free(g->vars[i].name);
    }
    g->var_count = write;
    g->scope_level--;
}

/*
 * purge_vars_at_or_above_scope — free all var-name strings whose scope_level
 * is >= `min_level`, without changing g->scope_level.
 */
static void purge_vars_at_or_above_scope(CodeGenerator *g, int min_level) {
    size_t write = 0;
    for (size_t i = 0; i < g->var_count; i++) {
        if (g->vars[i].scope_level < min_level)
            g->vars[write++] = g->vars[i];
        else
            ocl_free(g->vars[i].name);
    }
    g->var_count = write;
}

/* ══════════════════════════════════════════════════════════════════
   Loop context — break / continue backpatching
   ══════════════════════════════════════════════════════════════════ */

static void loop_push(CodeGenerator *g) {
    if (g->loop_depth >= CODEGEN_MAX_LOOP_DEPTH) {
        if (g->errors)
            error_add(g->errors, ERRK_LOGIC, ERROR_CODEGEN, (SourceLocation){0, 0, NULL},
                      "Loop nesting too deep");
        return;
    }
    LoopContext *ctx = &g->loop_stack[g->loop_depth++];
    ctx->continue_target = 0;
    ctx->continue_known  = false;
    ctx->break_count     = 0;
    ctx->continue_count  = 0;
}

static void loop_pop(CodeGenerator *g)           { if (g->loop_depth > 0) g->loop_depth--; }
static LoopContext *loop_current(CodeGenerator *g){ return g->loop_depth > 0 ? &g->loop_stack[g->loop_depth - 1] : NULL; }

static void loop_add_break(CodeGenerator *g, uint32_t idx) {
    LoopContext *ctx = loop_current(g);
    if (ctx && ctx->break_count < CODEGEN_MAX_BREAKS)
        ctx->breaks[ctx->break_count++].patch_idx = idx;
}

static void loop_add_continue(CodeGenerator *g, uint32_t idx) {
    LoopContext *ctx = loop_current(g);
    if (ctx && ctx->continue_count < CODEGEN_MAX_BREAKS)
        ctx->continues[ctx->continue_count++].patch_idx = idx;
}

static void loop_patch_breaks(CodeGenerator *g, uint32_t exit_ip) {
    LoopContext *ctx = loop_current(g);
    if (!ctx) return;
    for (int i = 0; i < ctx->break_count; i++)
        bytecode_patch(g->bytecode, ctx->breaks[i].patch_idx, exit_ip);
}

static void loop_patch_continues(CodeGenerator *g, uint32_t cont_ip) {
    LoopContext *ctx = loop_current(g);
    if (!ctx) return;
    for (int i = 0; i < ctx->continue_count; i++)
        bytecode_patch(g->bytecode, ctx->continues[i].patch_idx, cont_ip);
}

/* ══════════════════════════════════════════════════════════════════
   Forward declarations
   ══════════════════════════════════════════════════════════════════ */

static void emit_node(CodeGenerator *g, ASTNode *node);
static void emit_expr(CodeGenerator *g, ExprNode *expr);
static uint32_t emit_function_body(CodeGenerator *g, const char *function_name,
                                   ParamNode **params, size_t param_count,
                                   BlockNode *body, SourceLocation function_loc);
static bool fold_values_equal(Value left, Value right) {
    if (left.type == right.type) {
        switch (left.type) {
            case VALUE_INT:    return left.data.int_val == right.data.int_val;
            case VALUE_FLOAT:  return left.data.float_val == right.data.float_val;
            case VALUE_BOOL:   return left.data.bool_val == right.data.bool_val;
            case VALUE_CHAR:   return left.data.char_val == right.data.char_val;
            case VALUE_STRING: {
                const char *left_text = left.data.string_val ? left.data.string_val : "";
                const char *right_text = right.data.string_val ? right.data.string_val : "";
                return strcmp(left_text, right_text) == 0;
            }
            case VALUE_NULL:   return true;
            default:           return false;
        }
    }

    if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
        (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
        double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
        double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
        return left_number == right_number;
    }

    return false;
}

static bool fold_string_repeat(Value left, Value right, Value *out) {
    const char *source = NULL;
    int64_t count = 0;
    size_t source_len = 0;
    size_t total_len = 0;
    char *result = NULL;

    if (left.type == VALUE_STRING && right.type == VALUE_INT) {
        source = left.data.string_val ? left.data.string_val : "";
        count = right.data.int_val;
    } else if (left.type == VALUE_INT && right.type == VALUE_STRING) {
        source = right.data.string_val ? right.data.string_val : "";
        count = left.data.int_val;
    } else {
        return false;
    }

    if (count <= 0) {
        *out = value_string(ocl_strdup(""));
        return true;
    }

    source_len = strlen(source);
    total_len = source_len * (size_t)count;
    result = ocl_malloc(total_len + 1);
    for (int64_t i = 0; i < count; i++)
        memcpy(result + ((size_t)i * source_len), source, source_len);
    result[total_len] = '\0';
    *out = value_string(result);
    return true;
}

static bool fold_binary_value(const char *op, Value left, Value right, Value *out) {
    if (!strcmp(op, "+")) {
        if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
            const char *left_text = left.data.string_val ? left.data.string_val : "";
            const char *right_text = right.data.string_val ? right.data.string_val : "";
            size_t left_len = strlen(left_text);
            size_t right_len = strlen(right_text);
            char *result = ocl_malloc(left_len + right_len + 1);
            memcpy(result, left_text, left_len);
            memcpy(result + left_len, right_text, right_len + 1);
            *out = value_string(result);
            return true;
        }
        if (left.type == VALUE_STRING && right.type == VALUE_CHAR) {
            const char *left_text = left.data.string_val ? left.data.string_val : "";
            size_t left_len = strlen(left_text);
            char *result = ocl_malloc(left_len + 2);
            memcpy(result, left_text, left_len);
            result[left_len] = right.data.char_val;
            result[left_len + 1] = '\0';
            *out = value_string(result);
            return true;
        }
        if (left.type == VALUE_CHAR && right.type == VALUE_STRING) {
            const char *right_text = right.data.string_val ? right.data.string_val : "";
            size_t right_len = strlen(right_text);
            char *result = ocl_malloc(right_len + 2);
            result[0] = left.data.char_val;
            memcpy(result + 1, right_text, right_len + 1);
            *out = value_string(result);
            return true;
        }
        if (left.type == VALUE_INT && right.type == VALUE_INT) {
            *out = value_int(left.data.int_val + right.data.int_val);
            return true;
        }
        if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
            (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
            double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
            double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
            *out = value_float(left_number + right_number);
            return true;
        }
        return false;
    }

    if (!strcmp(op, "*")) {
        if (fold_string_repeat(left, right, out))
            return true;
        if (left.type == VALUE_INT && right.type == VALUE_INT) {
            *out = value_int(left.data.int_val * right.data.int_val);
            return true;
        }
        if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
            (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
            double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
            double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
            *out = value_float(left_number * right_number);
            return true;
        }
        return false;
    }

    if (!strcmp(op, "-")) {
        if (left.type == VALUE_INT && right.type == VALUE_INT) {
            *out = value_int(left.data.int_val - right.data.int_val);
            return true;
        }
        if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
            (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
            double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
            double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
            *out = value_float(left_number - right_number);
            return true;
        }
        return false;
    }

    if (!strcmp(op, "/")) {
        bool div_by_zero = (right.type == VALUE_FLOAT) ? (right.data.float_val == 0.0)
                                                       : (right.type == VALUE_INT && right.data.int_val == 0);
        if (div_by_zero)
            return false;
        if (left.type == VALUE_INT && right.type == VALUE_INT) {
            *out = value_int(left.data.int_val / right.data.int_val);
            return true;
        }
        if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
            (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
            double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
            double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
            *out = value_float(left_number / right_number);
            return true;
        }
        return false;
    }

    if (!strcmp(op, "%")) {
        if (left.type == VALUE_INT && right.type == VALUE_INT) {
            if (right.data.int_val == 0)
                return false;
            *out = value_int(left.data.int_val % right.data.int_val);
            return true;
        }
        if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
            (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
            double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
            double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
            if (right_number == 0.0)
                return false;
            *out = value_float(fmod(left_number, right_number));
            return true;
        }
        return false;
    }

    if (!strcmp(op, "==")) {
        *out = value_bool(fold_values_equal(left, right));
        return true;
    }

    if (!strcmp(op, "!=")) {
        *out = value_bool(!fold_values_equal(left, right));
        return true;
    }

    if (!strcmp(op, "<") || !strcmp(op, "<=") ||
        !strcmp(op, ">") || !strcmp(op, ">=")) {
        bool result = false;

        if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
            const char *left_text = left.data.string_val ? left.data.string_val : "";
            const char *right_text = right.data.string_val ? right.data.string_val : "";
            int compare = strcmp(left_text, right_text);
            if (!strcmp(op, "<")) result = compare < 0;
            else if (!strcmp(op, "<=")) result = compare <= 0;
            else if (!strcmp(op, ">")) result = compare > 0;
            else result = compare >= 0;
            *out = value_bool(result);
            return true;
        }

        if ((left.type == VALUE_INT || left.type == VALUE_FLOAT) &&
            (right.type == VALUE_INT || right.type == VALUE_FLOAT)) {
            double left_number = (left.type == VALUE_FLOAT) ? left.data.float_val : (double)left.data.int_val;
            double right_number = (right.type == VALUE_FLOAT) ? right.data.float_val : (double)right.data.int_val;
            if (!strcmp(op, "<")) result = left_number < right_number;
            else if (!strcmp(op, "<=")) result = left_number <= right_number;
            else if (!strcmp(op, ">")) result = left_number > right_number;
            else result = left_number >= right_number;
            *out = value_bool(result);
            return true;
        }

        return false;
    }

    if (!strcmp(op, "&") || !strcmp(op, "|") || !strcmp(op, "^") ||
        !strcmp(op, "<<") || !strcmp(op, ">>")) {
        if (left.type != VALUE_INT || right.type != VALUE_INT)
            return false;
        if (!strcmp(op, "&")) *out = value_int(left.data.int_val & right.data.int_val);
        else if (!strcmp(op, "|")) *out = value_int(left.data.int_val | right.data.int_val);
        else if (!strcmp(op, "^")) *out = value_int(left.data.int_val ^ right.data.int_val);
        else if (!strcmp(op, "<<")) *out = value_int(left.data.int_val << right.data.int_val);
        else *out = value_int(left.data.int_val >> right.data.int_val);
        return true;
    }

    return false;
}

static bool fold_unary_value(const char *op, Value operand, Value *out) {
    if (!strcmp(op, "-")) {
        if (operand.type == VALUE_INT) {
            *out = value_int(-operand.data.int_val);
            return true;
        }
        if (operand.type == VALUE_FLOAT) {
            *out = value_float(-operand.data.float_val);
            return true;
        }
        return false;
    }

    if (!strcmp(op, "!")) {
        *out = value_bool(!value_is_truthy(operand));
        return true;
    }

    if (!strcmp(op, "~")) {
        if (operand.type != VALUE_INT)
            return false;
        *out = value_int(~operand.data.int_val);
        return true;
    }

    return false;
}

static bool try_fold_expr(const ExprNode *expr, Value *out) {
    if (!expr || !out)
        return false;

    switch (expr->base.type) {
        case AST_LITERAL: {
            const LiteralNode *literal = (const LiteralNode *)expr;
            if (literal->value.type == VALUE_ARRAY ||
                literal->value.type == VALUE_STRUCT ||
                literal->value.type == VALUE_FUNCTION)
                return false;
            *out = value_own_copy(literal->value);
            return true;
        }

        case AST_UNARY_OP: {
            const UnaryOpNode *unary = (const UnaryOpNode *)expr;
            Value operand;
            bool ok;

            if (!try_fold_expr(unary->operand, &operand))
                return false;
            ok = fold_unary_value(unary->operator, operand, out);
            value_free(operand);
            return ok;
        }

        case AST_BIN_OP: {
            const BinOpNode *binary = (const BinOpNode *)expr;
            Value left;
            Value right;
            bool ok = false;

            if (!strcmp(binary->operator, "="))
                return false;

            if (!strcmp(binary->operator, "&&")) {
                if (!try_fold_expr(binary->left, &left))
                    return false;
                if (!value_is_truthy(left)) {
                    *out = value_bool(false);
                    value_free(left);
                    return true;
                }
                value_free(left);
                if (!try_fold_expr(binary->right, &right))
                    return false;
                *out = value_bool(value_is_truthy(right));
                value_free(right);
                return true;
            }

            if (!strcmp(binary->operator, "||")) {
                if (!try_fold_expr(binary->left, &left))
                    return false;
                if (value_is_truthy(left)) {
                    *out = value_bool(true);
                    value_free(left);
                    return true;
                }
                value_free(left);
                if (!try_fold_expr(binary->right, &right))
                    return false;
                *out = value_bool(value_is_truthy(right));
                value_free(right);
                return true;
            }

            if (!strcmp(binary->operator, "??")) {
                if (!try_fold_expr(binary->left, &left))
                    return false;
                if (left.type != VALUE_NULL) {
                    *out = value_own_copy(left);
                    value_free(left);
                    return true;
                }
                value_free(left);
                return try_fold_expr(binary->right, out);
            }

            if (!try_fold_expr(binary->left, &left))
                return false;
            if (!try_fold_expr(binary->right, &right)) {
                value_free(left);
                return false;
            }

            ok = fold_binary_value(binary->operator, left, right, out);
            value_free(left);
            value_free(right);
            return ok;
        }

        case AST_TERNARY: {
            const TernaryNode *ternary = (const TernaryNode *)expr;
            Value condition;

            if (!try_fold_expr(ternary->condition, &condition))
                return false;
            if (value_is_truthy(condition)) {
                value_free(condition);
                return try_fold_expr(ternary->true_expr, out);
            }
            value_free(condition);
            return try_fold_expr(ternary->false_expr, out);
        }

        default:
            return false;
    }
}

static bool has_emitted_module(CodeGenerator *g, const char *path) {
    if (!g || !path) return false;
    for (size_t i = 0; i < g->emitted_module_count; i++)
        if (!strcmp(g->emitted_modules[i], path)) return true;
    return false;
}

static void mark_emitted_module(CodeGenerator *g, const char *path) {
    if (!g || !path || has_emitted_module(g, path)) return;
    if (g->emitted_module_count >= g->emitted_module_capacity) {
        g->emitted_module_capacity = g->emitted_module_capacity ? g->emitted_module_capacity * 2 : 8;
        g->emitted_modules = ocl_realloc(g->emitted_modules,
                                         g->emitted_module_capacity * sizeof(char *));
    }
    g->emitted_modules[g->emitted_module_count++] = ocl_strdup(path);
}

static void predeclare_program(CodeGenerator *g, ProgramNode *program) {
    if (!g || !program) return;

    for (size_t i = 0; i < program->import_count; i++)
        predeclare_program(g, program->imports[i]);

    for (size_t i = 0; i < program->node_count; i++) {
        ASTNode *n = program->nodes[i];
        if (n->type == AST_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)n;
            if (lookup_global(g, v->name) < 0) add_global(g, v->name);
        } else if (n->type == AST_DECLARE) {
            DeclareNode *d = (DeclareNode *)n;
            if (lookup_global(g, d->name) < 0) add_global(g, d->name);
        } else if (n->type == AST_FUNC_DECL) {
            FuncDeclNode *f = (FuncDeclNode *)n;
            bytecode_add_function(g->bytecode, f->name, OCL_FUNC_UNRESOLVED, (int)f->param_count);
        }
    }
}

static void emit_program(CodeGenerator *g, ProgramNode *program) {
    if (!g || !program) return;
    if (program->module_path && has_emitted_module(g, program->module_path))
        return;

    if (program->module_path)
        mark_emitted_module(g, program->module_path);

    for (size_t i = 0; i < program->import_count; i++)
        emit_program(g, program->imports[i]);

    for (size_t i = 0; i < program->node_count; i++)
        if (program->nodes[i]->type == AST_FUNC_DECL)
            emit_node(g, program->nodes[i]);

    for (size_t i = 0; i < program->node_count; i++)
        if (program->nodes[i]->type != AST_FUNC_DECL)
            emit_node(g, program->nodes[i]);
}

static uint32_t emit_function_body(CodeGenerator *g, const char *function_name,
                                   ParamNode **params, size_t param_count,
                                   BlockNode *body, SourceLocation function_loc) {
    Bytecode *bc = g->bytecode;
    uint32_t fidx = bytecode_add_function(bc, function_name, 0, (int)param_count);

    if (bc->functions[fidx].start_ip != OCL_FUNC_UNRESOLVED && bc->functions[fidx].start_ip != 0)
        return fidx;

    uint32_t jump_over = (uint32_t)bc->instruction_count;
    bytecode_emit(bc, OP_JUMP, 0, 0, function_loc);
    uint32_t start_ip = (uint32_t)bc->instruction_count;
    bc->functions[fidx].start_ip = start_ip;

    bool saved_global = g->in_global_scope;
    int  saved_scope  = g->scope_level;

    g->in_global_scope = false;
    g->local_stack[g->local_stack_top++] = (int)param_count;
    g->scope_level++;
    g->function_stack[g->function_stack_top++].function_index = fidx;

    for (size_t i = 0; i < param_count; i++)
        add_param(g, params[i]->name, (int)i);

    if (body) {
        for (size_t i = 0; i < body->statement_count; i++)
            emit_node(g, body->statements[i]);
    }

    if (bc->instruction_count == 0 ||
        bc->instructions[bc->instruction_count - 1].opcode != OP_RETURN) {
        uint32_t ci = bytecode_add_constant(bc, value_null());
        bytecode_emit(bc, OP_PUSH_CONST, ci, 0, function_loc);
        bytecode_emit(bc, OP_RETURN, 0, 0, function_loc);
    }

    bc->functions[fidx].local_count = g->local_stack[g->local_stack_top - 1];

    purge_vars_at_or_above_scope(g, saved_scope + 1);

    g->function_stack_top--;
    g->local_stack_top--;
    g->scope_level = saved_scope;
    g->in_global_scope = saved_global;

    bytecode_patch(bc, jump_over, (uint32_t)bc->instruction_count);
    return fidx;
}

/* ══════════════════════════════════════════════════════════════════
   Expression code generation
   ══════════════════════════════════════════════════════════════════ */

static void emit_expr(CodeGenerator *g, ExprNode *expr) {
    if (!expr) return;
    Bytecode *bc = g->bytecode;
    Value folded;

    if (try_fold_expr(expr, &folded)) {
        uint32_t ci = bytecode_add_constant(bc, folded);
        bytecode_emit(bc, OP_PUSH_CONST, ci, 0, expr->base.location);
        value_free(folded);
        return;
    }

    switch (expr->base.type) {

    case AST_LITERAL: {
        LiteralNode *lit = (LiteralNode *)expr;
        uint32_t ci = bytecode_add_constant(bc, lit->value);
        bytecode_emit(bc, OP_PUSH_CONST, ci, 0, lit->base.location);
        break;
    }

    case AST_IDENTIFIER: {
        IdentifierNode *id = (IdentifierNode *)expr;
        VarSlot *var = resolve_var(g, id->name);
        if (var) {
            bytecode_emit(bc,
                          var->kind == VAR_SLOT_CAPTURE ? OP_LOAD_CAPTURE : OP_LOAD_VAR,
                          (uint32_t)var->slot,
                          0,
                          id->base.location);
            break;
        }
        int global = lookup_global(g, id->name);
        if (global >= 0) {
            bytecode_emit(bc, OP_LOAD_GLOBAL, (uint32_t)global, 0, id->base.location);
            break;
        }
        int fidx = bytecode_find_function(bc, id->name);
        if (fidx >= 0) {
            bytecode_emit(bc, OP_MAKE_FUNCTION, (uint32_t)fidx, 0, id->base.location);
            break;
        }
        if (g->errors)
            error_add(g->errors, ERRK_TYPE, ERROR_CODEGEN, id->base.location,
                      "Undefined variable '%s'", id->name);
        uint32_t ci = bytecode_add_constant(bc, value_null());
        bytecode_emit(bc, OP_PUSH_CONST, ci, 0, id->base.location);
        break;
    }

    case AST_BIN_OP: {
        BinOpNode *b = (BinOpNode *)expr;

        /* ── Assignment ──────────────────────────────────────────── */
        if (!strcmp(b->operator, "=")) {
            emit_expr(g, b->right);
            if (b->left && b->left->base.type == AST_IDENTIFIER) {
                IdentifierNode *id = (IdentifierNode *)b->left;
                VarSlot *var = resolve_var(g, id->name);
                if (var)
                    bytecode_emit(bc,
                                  var->kind == VAR_SLOT_CAPTURE ? OP_STORE_CAPTURE : OP_STORE_VAR,
                                  (uint32_t)var->slot,
                                  0,
                                  b->base.location);
                else {
                    int global = lookup_global(g, id->name);
                    if (global >= 0)
                        bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)global, 0, b->base.location);
                    else if (g->errors)
                        error_add(g->errors, ERRK_TYPE, ERROR_CODEGEN, b->base.location,
                                  "Cannot assign to undefined '%s'", id->name);
                }
            } else if (b->left && b->left->base.type == AST_INDEX_ACCESS) {
                IndexAccessNode *ia = (IndexAccessNode *)b->left;
                emit_expr(g, ia->array_expr);
                emit_expr(g, ia->index_expr);
                bytecode_emit(bc, OP_ARRAY_SET, 0, 0, b->base.location);
            } else if (b->left && b->left->base.type == AST_FIELD_ACCESS) {
                FieldAccessNode *fa = (FieldAccessNode *)b->left;
                uint32_t ci = bytecode_add_constant(bc, value_string_copy(fa->field_name));
                emit_expr(g, fa->object);
                bytecode_emit(bc, OP_STRUCT_SET, ci, 0, b->base.location);
            } else if (g->errors) {
                error_add(g->errors, ERRK_SYNTAX, ERROR_CODEGEN, b->base.location,
                          "Invalid assignment target");
            }
            break;
        }

        if (!strcmp(b->operator, "??")) {
            emit_expr(g, b->left);
            bytecode_emit(bc, OP_DUP, 0, 0, b->base.location);
            uint32_t jump_keep = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_NOT_NULL, 0, 0, b->base.location);
            bytecode_emit(bc, OP_POP, 0, 0, b->base.location);
            emit_expr(g, b->right);
            bytecode_patch(bc, jump_keep, (uint32_t)bc->instruction_count);
            break;
        }

        /* ── Short-circuit && ────────────────────────────────────── */
        if (!strcmp(b->operator, "&&")) {
            emit_expr(g, b->left);
            uint32_t jf1 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, b->base.location);

            emit_expr(g, b->right);
            uint32_t jf2 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, b->base.location);

            uint32_t ci_true = bytecode_add_constant(bc, value_bool(true));
            bytecode_emit(bc, OP_PUSH_CONST, ci_true, 0, b->base.location);
            uint32_t jmp_end = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP, 0, 0, b->base.location);

            uint32_t sc_false = (uint32_t)bc->instruction_count;
            bytecode_patch(bc, jf1, sc_false);
            bytecode_patch(bc, jf2, sc_false);
            uint32_t ci_false = bytecode_add_constant(bc, value_bool(false));
            bytecode_emit(bc, OP_PUSH_CONST, ci_false, 0, b->base.location);

            bytecode_patch(bc, jmp_end, (uint32_t)bc->instruction_count);
            break;
        }

        /* ── Short-circuit || ────────────────────────────────────── */
        if (!strcmp(b->operator, "||")) {
            emit_expr(g, b->left);
            uint32_t jt1 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_TRUE, 0, 0, b->base.location);

            emit_expr(g, b->right);
            uint32_t jt2 = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_TRUE, 0, 0, b->base.location);

            uint32_t ci_false = bytecode_add_constant(bc, value_bool(false));
            bytecode_emit(bc, OP_PUSH_CONST, ci_false, 0, b->base.location);
            uint32_t jmp_end = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP, 0, 0, b->base.location);

            uint32_t sc_true = (uint32_t)bc->instruction_count;
            bytecode_patch(bc, jt1, sc_true);
            bytecode_patch(bc, jt2, sc_true);
            uint32_t ci_true = bytecode_add_constant(bc, value_bool(true));
            bytecode_emit(bc, OP_PUSH_CONST, ci_true, 0, b->base.location);

            bytecode_patch(bc, jmp_end, (uint32_t)bc->instruction_count);
            break;
        }

        /* ── All other binary operators ──────────────────────────── */
        emit_expr(g, b->left);
        emit_expr(g, b->right);
        static const struct { const char *op; Opcode code; } map[] = {
            {"+",  OP_ADD},       {"-",  OP_SUBTRACT},
            {"*",  OP_MULTIPLY},  {"/",  OP_DIVIDE},
            {"%",  OP_MODULO},    {"==", OP_EQUAL},
            {"!=", OP_NOT_EQUAL}, {"<",  OP_LESS},
            {"<=", OP_LESS_EQUAL},{">" , OP_GREATER},
            {">=", OP_GREATER_EQUAL},
            {"&",  OP_BIT_AND},   {"|",  OP_BIT_OR},
            {"^",  OP_BIT_XOR},   {"<<", OP_LSHIFT},
            {">>", OP_RSHIFT},
            {NULL, 0}
        };
        for (int i = 0; map[i].op; i++)
            if (!strcmp(b->operator, map[i].op)) {
                bytecode_emit(bc, map[i].code, 0, 0, b->base.location);
                break;
            }
        break;
    }

    case AST_UNARY_OP: {
        UnaryOpNode *u = (UnaryOpNode *)expr;
        emit_expr(g, u->operand);
        if (!strcmp(u->operator, "-"))      bytecode_emit(bc, OP_NEGATE, 0, 0, u->base.location);
        else if (!strcmp(u->operator, "!")) bytecode_emit(bc, OP_NOT,    0, 0, u->base.location);
        else if (!strcmp(u->operator, "~")) bytecode_emit(bc, OP_BIT_NOT, 0, 0, u->base.location);
        break;
    }

    case AST_TERNARY: {
        TernaryNode *t = (TernaryNode *)expr;
        emit_expr(g, t->condition);
        uint32_t jump_false = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, t->base.location);

        emit_expr(g, t->true_expr);
        uint32_t jump_end = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, 0, 0, t->base.location);

        uint32_t false_ip = (uint32_t)bc->instruction_count;
        bytecode_patch(bc, jump_false, false_ip);
        emit_expr(g, t->false_expr);

        bytecode_patch(bc, jump_end, (uint32_t)bc->instruction_count);
        break;
    }

    case AST_CALL: {
        CallNode *c = (CallNode *)expr;
        bool direct_call = false;
        uint32_t direct_fidx = 0xFFFFFFFFu;

        if (c->callee && c->callee->base.type == AST_IDENTIFIER) {
            IdentifierNode *id = (IdentifierNode *)c->callee;
            if (!resolve_var(g, id->name) && lookup_global(g, id->name) < 0) {
                int fidx = bytecode_find_function(bc, id->name);
                if (fidx >= 0) {
                    direct_call = true;
                    direct_fidx = (uint32_t)fidx;
                }
            }
        }

        if (direct_call) {
            for (size_t i = 0; i < c->argument_count; i++)
                emit_expr(g, c->arguments[i]);
            bytecode_emit(bc, OP_CALL, direct_fidx, (uint32_t)c->argument_count, c->base.location);
        } else {
            emit_expr(g, c->callee);
            for (size_t i = 0; i < c->argument_count; i++)
                emit_expr(g, c->arguments[i]);
            bytecode_emit(bc, OP_CALL_VALUE, 0, (uint32_t)c->argument_count, c->base.location);
        }
        break;
    }

    case AST_ARRAY_LITERAL: {
        ArrayLiteralNode *al = (ArrayLiteralNode *)expr;
        for (size_t i = 0; i < al->element_count; i++)
            emit_expr(g, al->elements[i]);
        bytecode_emit(bc, OP_ARRAY_NEW, (uint32_t)al->element_count, 0, al->base.location);
        break;
    }

    case AST_INDEX_ACCESS: {
        IndexAccessNode *ia = (IndexAccessNode *)expr;
        emit_expr(g, ia->array_expr);
        emit_expr(g, ia->index_expr);
        bytecode_emit(bc, OP_ARRAY_GET, 0, 0, ia->base.location);
        break;
    }

    case AST_STRUCT_LITERAL: {
        StructLiteralNode *sl = (StructLiteralNode *)expr;
        uint32_t type_ci = bytecode_add_constant(bc, value_string_copy(sl->struct_name));
        for (size_t i = 0; i < sl->field_count; i++) {
            uint32_t field_ci = bytecode_add_constant(bc, value_string_copy(sl->field_names[i]));
            bytecode_emit(bc, OP_PUSH_CONST, field_ci, 0, sl->base.location);
            emit_expr(g, sl->field_values[i]);
        }
        bytecode_emit(bc, OP_STRUCT_NEW, type_ci, (uint32_t)sl->field_count, sl->base.location);
        break;
    }

    case AST_FIELD_ACCESS: {
        FieldAccessNode *fa = (FieldAccessNode *)expr;
        uint32_t field_ci = bytecode_add_constant(bc, value_string_copy(fa->field_name));
        emit_expr(g, fa->object);
        if (fa->is_optional) {
            bytecode_emit(bc, OP_DUP, 0, 0, fa->base.location);
            uint32_t jump_get = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_NOT_NULL, 0, 0, fa->base.location);
            uint32_t jump_end = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP, 0, 0, fa->base.location);
            bytecode_patch(bc, jump_get, (uint32_t)bc->instruction_count);
            bytecode_emit(bc, OP_STRUCT_GET, field_ci, 0, fa->base.location);
            bytecode_patch(bc, jump_end, (uint32_t)bc->instruction_count);
            break;
        }
        bytecode_emit(bc, OP_STRUCT_GET, field_ci, 0, fa->base.location);
        break;
    }

    case AST_FUNC_EXPR: {
        FuncExprNode *fn = (FuncExprNode *)expr;
        if (!fn->generated_name) {
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "__lambda_%u", g->lambda_counter++);
            fn->generated_name = ocl_strdup(name_buf);
        }
        uint32_t fidx = emit_function_body(g, fn->generated_name, fn->params, fn->param_count,
                                           fn->body, fn->base.location);
        bytecode_emit(bc, OP_MAKE_FUNCTION, (uint32_t)fidx, 0, fn->base.location);
        break;
    }

    default:
        emit_node(g, (ASTNode *)expr);
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
   Statement code generation
   ══════════════════════════════════════════════════════════════════ */

static void emit_node(CodeGenerator *g, ASTNode *node) {
    if (!node) return;
    Bytecode *bc = g->bytecode;

    switch (node->type) {

    case AST_LITERAL:
    case AST_IDENTIFIER:
    case AST_ARRAY_LITERAL:
    case AST_INDEX_ACCESS:
    case AST_STRUCT_LITERAL:
    case AST_FIELD_ACCESS:
    case AST_UNARY_OP:
    case AST_TERNARY:
    case AST_FUNC_EXPR:
        emit_expr(g, (ExprNode *)node);
        bytecode_emit(bc, OP_POP, 0, 0, node->location);
        break;

    case AST_BIN_OP: {
        BinOpNode *_b = (BinOpNode *)node;
        if (!strcmp(_b->operator, "=")) {
            emit_expr(g, (ExprNode *)node);
        } else {
            emit_expr(g, (ExprNode *)node);
            bytecode_emit(bc, OP_POP, 0, 0, node->location);
        }
        break;
    }

    case AST_CALL:
        emit_expr(g, (ExprNode *)node);
        bytecode_emit(bc, OP_POP, 0, 0, node->location);
        break;

    case AST_IMPORT:
    case AST_STRUCT_DECL:
        break;

    case AST_DECLARE: {
        DeclareNode *d = (DeclareNode *)node;
        if (g->in_global_scope) {
            if (lookup_global(g, d->name) < 0) {
                int slot = add_global(g, d->name);
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, d->base.location);
                bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)slot, 0, d->base.location);
            }
        } else {
            if (!lookup_current_function_var(g, d->name)) {
                int slot = add_local(g, d->name);
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, d->base.location);
                bytecode_emit(bc, OP_STORE_VAR, (uint32_t)slot, 0, d->base.location);
            }
        }
        break;
    }

    case AST_VAR_DECL: {
        VarDeclNode *v = (VarDeclNode *)node;
        if (g->in_global_scope) {
            int slot = lookup_global(g, v->name);
            if (slot < 0) slot = add_global(g, v->name);
            if (v->initializer)
                emit_expr(g, v->initializer);
            else {
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, v->base.location);
            }
            bytecode_emit(bc, OP_STORE_GLOBAL, (uint32_t)slot, 0, v->base.location);
        } else {
            int slot = add_local(g, v->name);
            if (v->initializer)
                emit_expr(g, v->initializer);
            else {
                uint32_t ci = bytecode_add_constant(bc, value_null());
                bytecode_emit(bc, OP_PUSH_CONST, ci, 0, v->base.location);
            }
            bytecode_emit(bc, OP_STORE_VAR, (uint32_t)slot, 0, v->base.location);
        }
        break;
    }

    case AST_FUNC_DECL: {
        FuncDeclNode *f = (FuncDeclNode *)node;
        emit_function_body(g, f->name, f->params, f->param_count, f->body, f->base.location);
        break;
    }

    case AST_BLOCK: {
        BlockNode *blk = (BlockNode *)node;
        enter_scope(g);
        for (size_t i = 0; i < blk->statement_count; i++)
            emit_node(g, blk->statements[i]);
        exit_scope(g);
        break;
    }

    case AST_IF_STMT: {
        uint32_t exit_jumps[256];
        int      exit_jump_count = 0;

        ASTNode *cur = node;
        while (cur && cur->type == AST_IF_STMT) {
            IfStmtNode *s = (IfStmtNode *)cur;

            emit_expr(g, s->condition);
            uint32_t jf_idx = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, s->base.location);

            enter_scope(g);
            if (s->then_block)
                for (size_t i = 0; i < s->then_block->statement_count; i++)
                    emit_node(g, s->then_block->statements[i]);
            exit_scope(g);

            if (exit_jump_count < 256) {
                uint32_t je_idx = (uint32_t)bc->instruction_count;
                bytecode_emit(bc, OP_JUMP, 0, 0, s->base.location);
                exit_jumps[exit_jump_count++] = je_idx;
            }

            bytecode_patch(bc, jf_idx, (uint32_t)bc->instruction_count);
            cur = s->else_next;
        }

        if (cur && cur->type == AST_BLOCK) {
            enter_scope(g);
            BlockNode *eb = (BlockNode *)cur;
            for (size_t i = 0; i < eb->statement_count; i++)
                emit_node(g, eb->statements[i]);
            exit_scope(g);
        }

        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        for (int i = 0; i < exit_jump_count; i++)
            bytecode_patch(bc, exit_jumps[i], exit_ip);
        break;
    }

    case AST_WHILE_LOOP: {
        /*
         * while (cond) { body }
         *
         * loop_start:
         *   <condition>
         *   JUMP_IF_FALSE → exit
         *   <body>
         * continue_ip:
         *   JUMP → loop_start
         * exit:
         */
        LoopNode *lp = (LoopNode *)node;
        loop_push(g);

        uint32_t loop_start = (uint32_t)bc->instruction_count;
        emit_expr(g, lp->condition);
        uint32_t jf_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);

        /* Emit body through AST_BLOCK handler for proper inner scope. */
        if (lp->body)
            emit_node(g, (ASTNode *)lp->body);

        uint32_t continue_ip = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);

        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        bytecode_patch(bc, jf_idx, exit_ip);
        loop_patch_breaks(g, exit_ip);
        loop_patch_continues(g, continue_ip);
        loop_pop(g);
        break;
    }

    case AST_DO_WHILE_LOOP: {
        LoopNode *lp = (LoopNode *)node;
        loop_push(g);

        uint32_t loop_start = (uint32_t)bc->instruction_count;
        if (lp->body)
            emit_node(g, (ASTNode *)lp->body);

        uint32_t continue_ip = (uint32_t)bc->instruction_count;
        emit_expr(g, lp->condition);
        uint32_t jf_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);
        bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);

        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        bytecode_patch(bc, jf_idx, exit_ip);
        loop_patch_breaks(g, exit_ip);
        loop_patch_continues(g, continue_ip);
        loop_pop(g);
        break;
    }

    case AST_FOR_LOOP: {
        /*
         * for (init; cond; incr) { body }
         *
         * The init variable (e.g. "int i = 0") is scoped to the loop:
         * it lives at scope_level N (the outer scope + 1), and the body
         * runs at scope_level N+1.  The init var is visible inside the
         * body because lookup_local searches all active scopes.
         *
         *   <init>               ← declares loop var at scope N
         * loop_start:
         *   [<condition>]        ← may reference loop var
         *   [JUMP_IF_FALSE → exit]
         *   <body>               ← emitted via AST_BLOCK (scope N+1)
         * continue_ip:
         *   <increment>          ← e.g. i = i + 1
         *   JUMP → loop_start
         * exit:
         */
        LoopNode *lp = (LoopNode *)node;
        loop_push(g);

        /*
         * Open a scope for the init variable.  Body variables will live
         * one level deeper (inside the AST_BLOCK handler below).
         */
        enter_scope(g);

        /* Emit the initializer — typically a VAR_DECL like "int i = 0". */
        if (lp->init) emit_node(g, lp->init);

        uint32_t loop_start = (uint32_t)bc->instruction_count;
        uint32_t jf_idx     = 0;
        bool     has_cond   = (lp->condition != NULL);
        if (has_cond) {
            emit_expr(g, lp->condition);
            jf_idx = (uint32_t)bc->instruction_count;
            bytecode_emit(bc, OP_JUMP_IF_FALSE, 0, 0, lp->base.location);
        }

        /*
         * Emit the body through the AST_BLOCK handler.
         * This gives body-local variables their own inner scope (N+1),
         * cleanly separated from the init variable at scope N.
         * The init variable is still visible because lookup_local searches
         * backwards through all active scopes.
         */
        if (lp->body)
            emit_node(g, (ASTNode *)lp->body);

        /*
         * continue_ip is where `continue` statements jump to — the increment
         * must run before re-testing the condition.
         */
        uint32_t continue_ip = (uint32_t)bc->instruction_count;

        if (lp->increment) {
            /*
             * The increment expression is typically a desugared i++ (i = i + 1).
             * Emit it as a statement: if it's an assignment, the result is
             * consumed by STORE_VAR/STORE_GLOBAL; otherwise pop any leftover value.
             */
            ExprNode *inc_expr = (ExprNode *)lp->increment;
            if (inc_expr->base.type == AST_BIN_OP &&
                !strcmp(((BinOpNode *)inc_expr)->operator, "=")) {
                /* Assignment desugared from i++/i-- or compound-assign.
                   STORE_VAR/STORE_GLOBAL pop the value, so no OP_POP needed. */
                emit_expr(g, inc_expr);
            } else {
                /* Any other expression (e.g. a bare function call) — discard result. */
                emit_expr(g, inc_expr);
                bytecode_emit(bc, OP_POP, 0, 0, lp->base.location);
            }
        }

        bytecode_emit(bc, OP_JUMP, loop_start, 0, lp->base.location);

        uint32_t exit_ip = (uint32_t)bc->instruction_count;
        if (has_cond) bytecode_patch(bc, jf_idx, exit_ip);
        loop_patch_breaks(g, exit_ip);
        loop_patch_continues(g, continue_ip);
        loop_pop(g);

        /* Close the init-variable scope. */
        exit_scope(g);
        break;
    }

    case AST_RETURN: {
        ReturnNode *r = (ReturnNode *)node;
        if (r->value)
            emit_expr(g, r->value);
        else {
            uint32_t ci = bytecode_add_constant(bc, value_null());
            bytecode_emit(bc, OP_PUSH_CONST, ci, 0, r->base.location);
        }
        bytecode_emit(bc, OP_RETURN, 0, 0, r->base.location);
        break;
    }

    case AST_BREAK: {
        if (!loop_current(g)) {
            if (g->errors)
                error_add(g->errors, ERRK_SYNTAX, ERROR_CODEGEN, node->location,
                          "'break' used outside of a loop");
            break;
        }
        uint32_t patch_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, 0, 0, node->location);
        loop_add_break(g, patch_idx);
        break;
    }

    case AST_CONTINUE: {
        if (!loop_current(g)) {
            if (g->errors)
                error_add(g->errors, ERRK_SYNTAX, ERROR_CODEGEN, node->location,
                          "'continue' used outside of a loop");
            break;
        }
        uint32_t patch_idx = (uint32_t)bc->instruction_count;
        bytecode_emit(bc, OP_JUMP, 0, 0, node->location);
        loop_add_continue(g, patch_idx);
        break;
    }

    default:
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════ */

CodeGenerator *codegen_create(ErrorCollector *errors) {
    CodeGenerator *g = ocl_malloc(sizeof(CodeGenerator));
    g->bytecode        = NULL;
    g->errors          = errors;
    g->vars            = NULL;
    g->var_count       = 0;
    g->var_cap         = 0;
    g->scope_level     = 0;
    g->local_stack[0]  = 0;
    g->local_stack_top = 1;
    g->in_global_scope = true;
    g->globals         = NULL;
    g->global_count    = 0;
    g->global_cap      = 0;
    g->function_stack_top = 0;
    g->lambda_counter  = 0;
    g->loop_depth      = 0;
    g->emitted_modules = NULL;
    g->emitted_module_count = 0;
    g->emitted_module_capacity = 0;
    return g;
}

void codegen_free(CodeGenerator *g) {
    if (!g) return;
    for (size_t i = 0; i < g->var_count;   i++) ocl_free(g->vars[i].name);
    for (size_t i = 0; i < g->global_count; i++) ocl_free(g->globals[i].name);
    ocl_free(g->vars);
    ocl_free(g->globals);
    for (size_t i = 0; i < g->emitted_module_count; i++) ocl_free(g->emitted_modules[i]);
    ocl_free(g->emitted_modules);
    ocl_free(g);
}

bool codegen_generate(CodeGenerator *g, ProgramNode *program, Bytecode *output) {
    if (!g || !program || !output) return false;
    size_t stdlib_count = 0;
    const StdlibEntry *stdlib_table = stdlib_get_table(&stdlib_count);

    g->bytecode        = output;
    g->in_global_scope = true;
    g->scope_level     = 0;
    g->var_count       = 0;
    g->global_count    = 0;
    g->local_stack[0]  = 0;
    g->local_stack_top = 1;
    g->function_stack_top = 0;
    g->lambda_counter = 0;
    g->loop_depth      = 0;
    for (size_t i = 0; i < g->emitted_module_count; i++) ocl_free(g->emitted_modules[i]);
    g->emitted_module_count = 0;

    for (size_t i = 0; i < stdlib_count; i++) {
        bytecode_add_function(output,
                              stdlib_table[i].name,
                              OCL_FUNC_BUILTIN,
                              stdlib_table[i].param_count);
    }

    predeclare_program(g, program);
    emit_program(g, program);

    int main_idx = bytecode_find_function(output, "main");
    if (main_idx >= 0) {
        SourceLocation entry = {1, 1, "entry"};
        bytecode_emit(output, OP_CALL, (uint32_t)main_idx, 0, entry);
        /* Pop the return value from main() — keep the stack clean before HALT. */
        bytecode_emit(output, OP_POP, 0, 0, entry);
    }

    SourceLocation halt_loc = {1, 1, "end"};
    bytecode_emit(output, OP_HALT, 0, 0, halt_loc);
    return true;
}
