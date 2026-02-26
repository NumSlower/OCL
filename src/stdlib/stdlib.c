/*
 * stdlib.c — OCL Standard Library (Phase 4)
 *
 * All handlers follow the same calling convention as vm.c's
 * builtin_print / builtin_printf:
 *   - argc arguments sit on the stack, first arg deepest
 *   - pop with vm_pop() in reverse order
 *   - MUST push exactly one return value before returning
 */

#include "stdlib.h"
#include "vm.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ═══════════════════════════════════════════════════════════════════
   Internal helpers
═══════════════════════════════════════════════════════════════════ */

/* Pop all argc arguments from the stack into a heap array.
   args[0] = first (leftmost) argument. Caller must ocl_free(). */
static Value *pop_args(VM *vm, int argc) {
    if (argc <= 0) return NULL;
    Value *args = ocl_malloc((size_t)argc * sizeof(Value));
    for (int i = argc - 1; i >= 0; i--)
        args[i] = vm_pop(vm);
    return args;
}

static double to_double(Value v) {
    switch (v.type) {
        case VALUE_INT:   return (double)v.data.int_val;
        case VALUE_FLOAT: return v.data.float_val;
        case VALUE_BOOL:  return v.data.bool_val ? 1.0 : 0.0;
        default:          return 0.0;
    }
}

static int64_t to_int64(Value v) {
    switch (v.type) {
        case VALUE_INT:    return v.data.int_val;
        case VALUE_FLOAT:  return (int64_t)v.data.float_val;
        case VALUE_BOOL:   return v.data.bool_val ? 1 : 0;
        case VALUE_STRING:
            return (v.data.string_val) ? (int64_t)strtoll(v.data.string_val, NULL, 10) : 0;
        default: return 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════
   I/O  (IDs 3-4)
═══════════════════════════════════════════════════════════════════ */

/* input(prompt?:String) -> String
   Prints optional prompt then reads one line from stdin. */
static void builtin_input(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc >= 1 && args[0].type == VALUE_STRING && args[0].data.string_val)
        printf("%s", args[0].data.string_val);
    fflush(stdout);
    ocl_free(args);

    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) {
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
    if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
    vm_push(vm, value_string(ocl_strdup(buf)));
}

/* readLine() -> String  — reads a line with no prompt */
static void builtin_readline(VM *vm, int argc) {
    builtin_input(vm, argc);
}

/* ═══════════════════════════════════════════════════════════════════
   Math  (IDs 10-20)
═══════════════════════════════════════════════════════════════════ */

/* abs(x) -> numeric */
static void builtin_abs(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_int(0)); return; }
    Value a = args[0];
    ocl_free(args);
    if (a.type == VALUE_INT)
        vm_push(vm, value_int(a.data.int_val < 0 ? -a.data.int_val : a.data.int_val));
    else
        vm_push(vm, value_float(fabs(to_double(a))));
}

/* sqrt(x) -> Float */
static void builtin_sqrt(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    if (x < 0.0) {
        fprintf(stderr, "RUNTIME ERROR: sqrt() called with negative number\n");
        vm_push(vm, value_float(0.0));
    } else {
        vm_push(vm, value_float(sqrt(x)));
    }
}

/* pow(base, exp) -> Float */
static void builtin_pow(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double base = (argc >= 1) ? to_double(args[0]) : 0.0;
    double exp  = (argc >= 2) ? to_double(args[1]) : 1.0;
    ocl_free(args);
    vm_push(vm, value_float(pow(base, exp)));
}

/* sin(x) -> Float */
static void builtin_sin(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    vm_push(vm, value_float(sin(x)));
}

/* cos(x) -> Float */
static void builtin_cos(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    vm_push(vm, value_float(cos(x)));
}

/* tan(x) -> Float */
static void builtin_tan(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    vm_push(vm, value_float(tan(x)));
}

/* floor(x) -> Float */
static void builtin_floor(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    vm_push(vm, value_float(floor(x)));
}

/* ceil(x) -> Float */
static void builtin_ceil(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    vm_push(vm, value_float(ceil(x)));
}

/* round(x) -> Float */
static void builtin_round(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double x = (argc >= 1) ? to_double(args[0]) : 0.0;
    ocl_free(args);
    vm_push(vm, value_float(round(x)));
}

/* max(a, b) -> numeric */
static void builtin_max(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2) { vm_push(vm, argc >= 1 ? args[0] : value_null()); ocl_free(args); return; }
    Value a = args[0], b = args[1];
    ocl_free(args);
    if (a.type == VALUE_INT && b.type == VALUE_INT)
        vm_push(vm, value_int(a.data.int_val >= b.data.int_val ? a.data.int_val : b.data.int_val));
    else {
        double da = to_double(a), db = to_double(b);
        vm_push(vm, value_float(da >= db ? da : db));
    }
}

/* min(a, b) -> numeric */
static void builtin_min(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2) { vm_push(vm, argc >= 1 ? args[0] : value_null()); ocl_free(args); return; }
    Value a = args[0], b = args[1];
    ocl_free(args);
    if (a.type == VALUE_INT && b.type == VALUE_INT)
        vm_push(vm, value_int(a.data.int_val <= b.data.int_val ? a.data.int_val : b.data.int_val));
    else {
        double da = to_double(a), db = to_double(b);
        vm_push(vm, value_float(da <= db ? da : db));
    }
}

/* ═══════════════════════════════════════════════════════════════════
   String  (IDs 30-38)
═══════════════════════════════════════════════════════════════════ */

/* strLen(s) -> Int */
static void builtin_strlen(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) {
        ocl_free(args); vm_push(vm, value_int(0)); return;
    }
    int64_t len = (int64_t)(args[0].data.string_val ? strlen(args[0].data.string_val) : 0);
    ocl_free(args);
    vm_push(vm, value_int(len));
}

/* substr(s, start, length?) -> String  (0-based start) */
static void builtin_substr(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2 || args[0].type != VALUE_STRING) {
        ocl_free(args); vm_push(vm, value_string(ocl_strdup(""))); return;
    }
    const char *s    = args[0].data.string_val ? args[0].data.string_val : "";
    size_t      slen = strlen(s);
    int64_t     start  = to_int64(args[1]);
    int64_t     length = (argc >= 3) ? to_int64(args[2]) : (int64_t)slen;
    ocl_free(args);

    if (start < 0) start = 0;
    if ((size_t)start >= slen) { vm_push(vm, value_string(ocl_strdup(""))); return; }
    if (length < 0) length = 0;
    if ((size_t)(start + length) > slen) length = (int64_t)(slen - (size_t)start);

    char *result = ocl_malloc((size_t)length + 1);
    memcpy(result, s + start, (size_t)length);
    result[length] = '\0';
    vm_push(vm, value_string(result));
}

/* toUpperCase(s) -> String */
static void builtin_toupper(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) {
        ocl_free(args); vm_push(vm, value_string(ocl_strdup(""))); return;
    }
    char *result = ocl_strdup(args[0].data.string_val ? args[0].data.string_val : "");
    ocl_free(args);
    for (char *p = result; *p; p++) *p = (char)toupper((unsigned char)*p);
    vm_push(vm, value_string(result));
}

/* toLowerCase(s) -> String */
static void builtin_tolower(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) {
        ocl_free(args); vm_push(vm, value_string(ocl_strdup(""))); return;
    }
    char *result = ocl_strdup(args[0].data.string_val ? args[0].data.string_val : "");
    ocl_free(args);
    for (char *p = result; *p; p++) *p = (char)tolower((unsigned char)*p);
    vm_push(vm, value_string(result));
}

/* strContains(haystack, needle) -> Bool */
static void builtin_strcontains(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2) { ocl_free(args); vm_push(vm, value_bool(false)); return; }
    const char *hay    = (args[0].type == VALUE_STRING && args[0].data.string_val) ? args[0].data.string_val : "";
    const char *needle = (args[1].type == VALUE_STRING && args[1].data.string_val) ? args[1].data.string_val : "";
    bool found = (strstr(hay, needle) != NULL);
    ocl_free(args);
    vm_push(vm, value_bool(found));
}

/* strIndexOf(haystack, needle) -> Int  (-1 if not found) */
static void builtin_strindexof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2) { ocl_free(args); vm_push(vm, value_int(-1)); return; }
    const char *hay    = (args[0].type == VALUE_STRING && args[0].data.string_val) ? args[0].data.string_val : "";
    const char *needle = (args[1].type == VALUE_STRING && args[1].data.string_val) ? args[1].data.string_val : "";
    const char *pos    = strstr(hay, needle);
    ocl_free(args);
    vm_push(vm, value_int(pos ? (int64_t)(pos - hay) : -1));
}

/* strReplace(s, old, new) -> String  — replaces ALL occurrences */
static void builtin_strreplace(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 3 || args[0].type != VALUE_STRING) {
        Value r = (argc >= 1 && args[0].type == VALUE_STRING)
                  ? value_string(ocl_strdup(args[0].data.string_val ? args[0].data.string_val : ""))
                  : value_string(ocl_strdup(""));
        ocl_free(args);
        vm_push(vm, r);
        return;
    }

    const char *src     = args[0].data.string_val ? args[0].data.string_val : "";
    const char *old_str = (args[1].type == VALUE_STRING && args[1].data.string_val) ? args[1].data.string_val : "";
    const char *new_str = (args[2].type == VALUE_STRING && args[2].data.string_val) ? args[2].data.string_val : "";
    ocl_free(args);

    size_t old_len = strlen(old_str);
    if (old_len == 0) { vm_push(vm, value_string(ocl_strdup(src))); return; }

    /* Count occurrences to pre-allocate */
    size_t count = 0;
    const char *cur = src;
    while ((cur = strstr(cur, old_str)) != NULL) { count++; cur += old_len; }

    size_t new_len    = strlen(new_str);
    size_t src_len    = strlen(src);
    size_t result_len = src_len + count * (new_len > old_len ? new_len - old_len : old_len - new_len) + 1;
    char  *result     = ocl_malloc(result_len);
    char  *dest       = result;

    cur = src;
    const char *found;
    while ((found = strstr(cur, old_str)) != NULL) {
        size_t chunk = (size_t)(found - cur);
        memcpy(dest, cur, chunk); dest += chunk;
        memcpy(dest, new_str, new_len); dest += new_len;
        cur = found + old_len;
    }
    strcpy(dest, cur);
    vm_push(vm, value_string(result));
}

/* strTrim(s) -> String */
static void builtin_strtrim(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) {
        ocl_free(args); vm_push(vm, value_string(ocl_strdup(""))); return;
    }
    const char *s = args[0].data.string_val ? args[0].data.string_val : "";
    ocl_free(args);
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) len--;
    char *result = ocl_malloc(len + 1);
    memcpy(result, s, len);
    result[len] = '\0';
    vm_push(vm, value_string(result));
}

/* strSplit(s, delimiter) -> Int  (returns token count; array support in Phase 5) */
static void builtin_strsplit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2 || args[0].type != VALUE_STRING) {
        ocl_free(args); vm_push(vm, value_int(0)); return;
    }
    const char *s     = args[0].data.string_val ? args[0].data.string_val : "";
    const char *delim = (args[1].type == VALUE_STRING && args[1].data.string_val) ? args[1].data.string_val : " ";
    ocl_free(args);

    char   *copy  = ocl_strdup(s);
    int64_t count = 0;
    char   *tok   = strtok(copy, delim);
    while (tok) { count++; tok = strtok(NULL, delim); }
    ocl_free(copy);
    vm_push(vm, value_int(count));
}

/* ═══════════════════════════════════════════════════════════════════
   Type conversions  (IDs 40-44)
═══════════════════════════════════════════════════════════════════ */

/* toInt(x) -> Int */
static void builtin_to_int(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    Value a = (argc >= 1) ? args[0] : value_null();
    ocl_free(args);
    vm_push(vm, value_int(to_int64(a)));
}

/* toFloat(x) -> Float */
static void builtin_to_float(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    Value a = (argc >= 1) ? args[0] : value_null();
    ocl_free(args);
    vm_push(vm, value_float(to_double(a)));
}

/* toString(x) -> String */
static void builtin_to_string(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_string(ocl_strdup(""))); return; }
    char *s = value_to_string(args[0]);
    ocl_free(args);
    vm_push(vm, value_string(ocl_strdup(s)));
}

/* toBool(x) -> Bool */
static void builtin_to_bool(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_bool(false)); return; }
    Value a = args[0];
    ocl_free(args);
    bool result;
    switch (a.type) {
        case VALUE_BOOL:   result = a.data.bool_val; break;
        case VALUE_INT:    result = (a.data.int_val  != 0); break;
        case VALUE_FLOAT:  result = (a.data.float_val != 0.0); break;
        case VALUE_STRING: result = (a.data.string_val && a.data.string_val[0] != '\0'); break;
        default:           result = false; break;
    }
    vm_push(vm, value_bool(result));
}

/* typeOf(x) -> String */
static void builtin_typeof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_string(ocl_strdup("null"))); return; }
    const char *name;
    switch (args[0].type) {
        case VALUE_INT:    name = "Int";    break;
        case VALUE_FLOAT:  name = "Float";  break;
        case VALUE_STRING: name = "String"; break;
        case VALUE_BOOL:   name = "Bool";   break;
        case VALUE_CHAR:   name = "Char";   break;
        case VALUE_NULL:   name = "null";   break;
        default:           name = "unknown"; break;
    }
    ocl_free(args);
    vm_push(vm, value_string(ocl_strdup(name)));
}

/* ═══════════════════════════════════════════════════════════════════
   Utilities  (IDs 50-56)
═══════════════════════════════════════════════════════════════════ */

/* exit(code?) */
static void builtin_exit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int code = (argc >= 1) ? (int)to_int64(args[0]) : 0;
    ocl_free(args);
    vm->halted    = true;
    vm->exit_code = code;
    vm_push(vm, value_null());
}

/* assert(cond, message?) */
static void builtin_assert(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_null()); return; }

    bool ok = value_is_truthy(args[0]);
    if (!ok) {
        if (argc >= 2 && args[1].type == VALUE_STRING && args[1].data.string_val)
            fprintf(stderr, "ASSERTION FAILED: %s\n", args[1].data.string_val);
        else
            fprintf(stderr, "ASSERTION FAILED\n");
        ocl_free(args);
        vm->halted    = true;
        vm->exit_code = 1;
        vm_push(vm, value_null());
        return;
    }
    ocl_free(args);
    vm_push(vm, value_null());
}

/* isNull(x) -> Bool */
static void builtin_is_null(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    bool r = (argc < 1 || args[0].type == VALUE_NULL);
    ocl_free(args);
    vm_push(vm, value_bool(r));
}

/* isInt(x) -> Bool */
static void builtin_is_int(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    bool r = (argc >= 1 && args[0].type == VALUE_INT);
    ocl_free(args);
    vm_push(vm, value_bool(r));
}

/* isFloat(x) -> Bool */
static void builtin_is_float(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    bool r = (argc >= 1 && args[0].type == VALUE_FLOAT);
    ocl_free(args);
    vm_push(vm, value_bool(r));
}

/* isString(x) -> Bool */
static void builtin_is_string(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    bool r = (argc >= 1 && args[0].type == VALUE_STRING);
    ocl_free(args);
    vm_push(vm, value_bool(r));
}

/* isBool(x) -> Bool */
static void builtin_is_bool(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    bool r = (argc >= 1 && args[0].type == VALUE_BOOL);
    ocl_free(args);
    vm_push(vm, value_bool(r));
}

/* ═══════════════════════════════════════════════════════════════════
   Dispatch table
═══════════════════════════════════════════════════════════════════ */

static const StdlibEntry STDLIB_TABLE[] = {
    /* I/O */
    { BUILTIN_INPUT,        "input",        builtin_input       },
    { BUILTIN_READLINE,     "readLine",     builtin_readline    },
    /* Math */
    { BUILTIN_ABS,          "abs",          builtin_abs         },
    { BUILTIN_SQRT,         "sqrt",         builtin_sqrt        },
    { BUILTIN_POW,          "pow",          builtin_pow         },
    { BUILTIN_SIN,          "sin",          builtin_sin         },
    { BUILTIN_COS,          "cos",          builtin_cos         },
    { BUILTIN_TAN,          "tan",          builtin_tan         },
    { BUILTIN_FLOOR,        "floor",        builtin_floor       },
    { BUILTIN_CEIL,         "ceil",         builtin_ceil        },
    { BUILTIN_ROUND,        "round",        builtin_round       },
    { BUILTIN_MAX,          "max",          builtin_max         },
    { BUILTIN_MIN,          "min",          builtin_min         },
    /* String */
    { BUILTIN_STRLEN,       "strLen",       builtin_strlen      },
    { BUILTIN_SUBSTR,       "substr",       builtin_substr      },
    { BUILTIN_TOUPPER,      "toUpperCase",  builtin_toupper     },
    { BUILTIN_TOLOWER,      "toLowerCase",  builtin_tolower     },
    { BUILTIN_STRCONTAINS,  "strContains",  builtin_strcontains },
    { BUILTIN_STRINDEXOF,   "strIndexOf",   builtin_strindexof  },
    { BUILTIN_STRREPLACE,   "strReplace",   builtin_strreplace  },
    { BUILTIN_STRTRIM,      "strTrim",      builtin_strtrim     },
    { BUILTIN_STRSPLIT,     "strSplit",     builtin_strsplit    },
    /* Type conversions */
    { BUILTIN_TO_INT,       "toInt",        builtin_to_int      },
    { BUILTIN_TO_FLOAT,     "toFloat",      builtin_to_float    },
    { BUILTIN_TO_STRING,    "toString",     builtin_to_string   },
    { BUILTIN_TO_BOOL,      "toBool",       builtin_to_bool     },
    { BUILTIN_TYPEOF,       "typeOf",       builtin_typeof      },
    /* Utilities */
    { BUILTIN_EXIT,         "exit",         builtin_exit        },
    { BUILTIN_ASSERT,       "assert",       builtin_assert      },
    { BUILTIN_IS_NULL,      "isNull",       builtin_is_null     },
    { BUILTIN_IS_INT,       "isInt",        builtin_is_int      },
    { BUILTIN_IS_FLOAT,     "isFloat",      builtin_is_float    },
    { BUILTIN_IS_STRING,    "isString",     builtin_is_string   },
    { BUILTIN_IS_BOOL,      "isBool",       builtin_is_bool     },
};

static const size_t STDLIB_TABLE_SIZE =
    sizeof(STDLIB_TABLE) / sizeof(STDLIB_TABLE[0]);

/* ═══════════════════════════════════════════════════════════════════
   Public API
═══════════════════════════════════════════════════════════════════ */

void stdlib_init(void)    { /* reserved for future use */ }
void stdlib_cleanup(void) { /* reserved for future use */ }

bool stdlib_dispatch(VM *vm, int id, int argc) {
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++) {
        if (STDLIB_TABLE[i].id == id) {
            STDLIB_TABLE[i].fn(vm, argc);
            return true;
        }
    }
    return false;
}

const StdlibEntry *stdlib_lookup_by_name(const char *name) {
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++)
        if (strcmp(STDLIB_TABLE[i].name, name) == 0)
            return &STDLIB_TABLE[i];
    return NULL;
}

const StdlibEntry *stdlib_get_table(size_t *out_count) {
    if (out_count) *out_count = STDLIB_TABLE_SIZE;
    return STDLIB_TABLE;
}