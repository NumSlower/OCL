#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "common.h"
#include "ocl_stdlib.h"
#include "vm.h"

/* Free a pop_args() buffer */
#define FREE_ARGS(arr, n) do { for (int _i = 0; _i < (n); _i++) value_free((arr)[_i]); ocl_free(arr); } while(0)

static Value *pop_args(VM *vm, int argc) {
    if (argc <= 0) return NULL;
    Value *args = ocl_malloc((size_t)argc * sizeof(Value));
    for (int i = argc - 1; i >= 0; i--) args[i] = vm_pop(vm);
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
        case VALUE_CHAR:   return (int64_t)(unsigned char)v.data.char_val;  // <-- ADD THIS
        case VALUE_STRING: return v.data.string_val ? (int64_t)strtoll(v.data.string_val, NULL, 10) : 0;
        default: return 0;
    }
}

static void builtin_input(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc >= 1 && args[0].type == VALUE_STRING && args[0].data.string_val)
        printf("%s", args[0].data.string_val);
    fflush(stdout);
    FREE_ARGS(args, argc);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) { vm_push(vm, value_string(ocl_strdup(""))); return; }
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
    if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
    vm_push(vm, value_string(ocl_strdup(buf)));
}
static void builtin_readline(VM *vm, int argc) { builtin_input(vm, argc); }

static void builtin_abs(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_int(0)); return; }
    Value a = args[0]; FREE_ARGS(args, argc);
    if (a.type == VALUE_INT) vm_push(vm, value_int(a.data.int_val < 0 ? -a.data.int_val : a.data.int_val));
    else vm_push(vm, value_float(fabs(to_double(a))));
}
static void builtin_sqrt(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); double x = argc >= 1 ? to_double(args[0]) : 0.0;
    FREE_ARGS(args, argc);
    vm_push(vm, value_float(x < 0.0 ? 0.0 : sqrt(x)));
}
static void builtin_pow(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    double base = argc >= 1 ? to_double(args[0]) : 0.0, exp = argc >= 2 ? to_double(args[1]) : 1.0;
    FREE_ARGS(args, argc);
    vm_push(vm, value_float(pow(base, exp)));
}
#define MATH1(name, fn) \
static void builtin_##name(VM *vm, int argc) { \
    Value *a = pop_args(vm, argc); double x = argc >= 1 ? to_double(a[0]) : 0; \
    FREE_ARGS(a, argc); \
    vm_push(vm, value_float(fn(x))); \
}
MATH1(sin,sin) MATH1(cos,cos) MATH1(tan,tan) MATH1(floor,floor) MATH1(ceil,ceil) MATH1(round,round)
#undef MATH1

static void builtin_max(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2) { Value r = argc >= 1 ? args[0] : value_null(); for (int i = 1; i < argc; i++) value_free(args[i]); ocl_free(args); vm_push(vm, r); return; }
    Value a = args[0], b = args[1]; FREE_ARGS(args, argc);
    if (a.type==VALUE_INT&&b.type==VALUE_INT) vm_push(vm,value_int(a.data.int_val>=b.data.int_val?a.data.int_val:b.data.int_val));
    else { double da=to_double(a),db=to_double(b); vm_push(vm,value_float(da>=db?da:db)); }
}
static void builtin_min(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2) { Value r = argc >= 1 ? args[0] : value_null(); for (int i = 1; i < argc; i++) value_free(args[i]); ocl_free(args); vm_push(vm, r); return; }
    Value a = args[0], b = args[1]; FREE_ARGS(args, argc);
    if (a.type==VALUE_INT&&b.type==VALUE_INT) vm_push(vm,value_int(a.data.int_val<=b.data.int_val?a.data.int_val:b.data.int_val));
    else { double da=to_double(a),db=to_double(b); vm_push(vm,value_float(da<=db?da:db)); }
}

static void builtin_strlen(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); int64_t len = 0;
    if (argc >= 1 && args[0].type == VALUE_STRING) len = (int64_t)(args[0].data.string_val ? strlen(args[0].data.string_val) : 0);
    FREE_ARGS(args, argc);
    vm_push(vm, value_int(len));
}
static void builtin_substr(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2 || args[0].type != VALUE_STRING) { FREE_ARGS(args, argc); vm_push(vm, value_string(ocl_strdup(""))); return; }
    const char *sp = args[0].data.string_val ? args[0].data.string_val : ""; size_t slen = strlen(sp);
    int64_t start = to_int64(args[1]), length = (argc >= 3) ? to_int64(args[2]) : (int64_t)slen;
    char *s = ocl_strdup(sp);
    FREE_ARGS(args, argc);
    if (start < 0) start = 0;
    if ((size_t)start >= slen) { ocl_free(s); vm_push(vm, value_string(ocl_strdup(""))); return; }
    if (length < 0) length = 0;
    if ((size_t)(start + length) > slen) length = (int64_t)(slen - (size_t)start);
    char *result = ocl_malloc((size_t)length + 1); memcpy(result, s + start, (size_t)length); result[length] = '\0';
    ocl_free(s); vm_push(vm, value_string(result));
}
static void builtin_toupper(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) { FREE_ARGS(args, argc); vm_push(vm, value_string(ocl_strdup(""))); return; }
    char *result = ocl_strdup(args[0].data.string_val ? args[0].data.string_val : "");
    FREE_ARGS(args, argc);
    for (char *p = result; *p; p++) *p = (char)toupper((unsigned char)*p);
    vm_push(vm, value_string(result));
}
static void builtin_tolower(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) { FREE_ARGS(args, argc); vm_push(vm, value_string(ocl_strdup(""))); return; }
    char *result = ocl_strdup(args[0].data.string_val ? args[0].data.string_val : "");
    FREE_ARGS(args, argc);
    for (char *p = result; *p; p++) *p = (char)tolower((unsigned char)*p);
    vm_push(vm, value_string(result));
}
static void builtin_strcontains(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); bool found = false;
    if (argc >= 2) {
        char *hay = ocl_strdup((args[0].type==VALUE_STRING&&args[0].data.string_val)?args[0].data.string_val:"");
        char *needle = ocl_strdup((args[1].type==VALUE_STRING&&args[1].data.string_val)?args[1].data.string_val:"");
        FREE_ARGS(args, argc);
        found = (strstr(hay, needle) != NULL); ocl_free(hay); ocl_free(needle);
    } else { FREE_ARGS(args, argc); }
    vm_push(vm, value_bool(found));
}
static void builtin_strindexof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); int64_t idx = -1;
    if (argc >= 2) {
        char *hay = ocl_strdup((args[0].type==VALUE_STRING&&args[0].data.string_val)?args[0].data.string_val:"");
        char *needle = ocl_strdup((args[1].type==VALUE_STRING&&args[1].data.string_val)?args[1].data.string_val:"");
        FREE_ARGS(args, argc);
        const char *pos = strstr(hay, needle); if (pos) idx = (int64_t)(pos - hay);
        ocl_free(hay); ocl_free(needle);
    } else { FREE_ARGS(args, argc); }
    vm_push(vm, value_int(idx));
}
static void builtin_strreplace(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 3 || args[0].type != VALUE_STRING) {
        Value r = (argc >= 1 && args[0].type == VALUE_STRING) ? value_string(ocl_strdup(args[0].data.string_val?args[0].data.string_val:"")) : value_string(ocl_strdup(""));
        FREE_ARGS(args, argc); vm_push(vm, r); return;
    }
    char *src = ocl_strdup(args[0].data.string_val?args[0].data.string_val:"");
    char *old_str = ocl_strdup((args[1].type==VALUE_STRING&&args[1].data.string_val)?args[1].data.string_val:"");
    char *new_str = ocl_strdup((args[2].type==VALUE_STRING&&args[2].data.string_val)?args[2].data.string_val:"");
    FREE_ARGS(args, argc);
    size_t old_len=strlen(old_str),new_len=strlen(new_str),src_len=strlen(src);
    if (old_len == 0) { ocl_free(old_str); ocl_free(new_str); vm_push(vm, value_string(src)); return; }
    size_t count = 0; const char *cur = src;
    while ((cur = strstr(cur, old_str)) != NULL) { count++; cur += old_len; }
    size_t result_len = src_len + count*(new_len>old_len?new_len-old_len:old_len-new_len)+1;
    char *result = ocl_malloc(result_len); char *dest = result; cur = src;
    const char *found;
    while ((found = strstr(cur, old_str)) != NULL) {
        size_t chunk=(size_t)(found-cur); memcpy(dest,cur,chunk); dest+=chunk;
        memcpy(dest,new_str,new_len); dest+=new_len; cur=found+old_len;
    }
    strcpy(dest, cur);
    ocl_free(src); ocl_free(old_str); ocl_free(new_str);
    vm_push(vm, value_string(result));
}
static void builtin_strtrim(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_STRING) { FREE_ARGS(args, argc); vm_push(vm, value_string(ocl_strdup(""))); return; }
    char *s = ocl_strdup(args[0].data.string_val ? args[0].data.string_val : "");
    FREE_ARGS(args, argc);
    char *p = s; while (isspace((unsigned char)*p)) p++;
    size_t len = strlen(p); while (len > 0 && isspace((unsigned char)p[len-1])) len--;
    char *result = ocl_malloc(len + 1); memcpy(result, p, len); result[len] = '\0';
    ocl_free(s); vm_push(vm, value_string(result));
}
static void builtin_strsplit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2 || args[0].type != VALUE_STRING) { FREE_ARGS(args, argc); vm_push(vm, value_int(0)); return; }
    char *copy = ocl_strdup(args[0].data.string_val ? args[0].data.string_val : "");
    char *delim = ocl_strdup((args[1].type==VALUE_STRING&&args[1].data.string_val)?args[1].data.string_val:" ");
    FREE_ARGS(args, argc);
    int64_t count = 0; char *tok = strtok(copy, delim);
    while (tok) { count++; tok = strtok(NULL, delim); }
    ocl_free(copy); ocl_free(delim); vm_push(vm, value_int(count));
}

static void builtin_to_int(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); int64_t r = argc >= 1 ? to_int64(args[0]) : 0;
    FREE_ARGS(args, argc); vm_push(vm, value_int(r));
}
static void builtin_to_float(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); double r = argc >= 1 ? to_double(args[0]) : 0.0;
    FREE_ARGS(args, argc); vm_push(vm, value_float(r));
}
static void builtin_to_string(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_string(ocl_strdup(""))); return; }
    char *s = value_to_string(args[0]); Value result = value_string(ocl_strdup(s));
    FREE_ARGS(args, argc); vm_push(vm, result);
}
static void builtin_to_bool(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); bool r = false;
    if (argc >= 1) {
        switch(args[0].type) {
            case VALUE_BOOL:   r=args[0].data.bool_val; break;
            case VALUE_INT:    r=(args[0].data.int_val!=0); break;
            case VALUE_FLOAT:  r=(args[0].data.float_val!=0.0); break;
            case VALUE_STRING: r=(args[0].data.string_val&&args[0].data.string_val[0]!='\0'); break;
            default: break;
        }
    }
    FREE_ARGS(args, argc); vm_push(vm, value_bool(r));
}
static void builtin_typeof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); const char *name = "null";
    if (argc >= 1) {
        switch(args[0].type) {
            case VALUE_INT:    name="Int"; break; case VALUE_FLOAT: name="Float"; break;
            case VALUE_STRING: name="String"; break; case VALUE_BOOL: name="Bool"; break;
            case VALUE_CHAR:   name="Char"; break; case VALUE_ARRAY: name="Array"; break;
            default: name="null"; break;
        }
    }
    FREE_ARGS(args, argc);
    vm_push(vm, value_string(ocl_strdup(name)));
}

static void builtin_exit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc); int code = argc >= 1 ? (int)to_int64(args[0]) : 0;
    FREE_ARGS(args, argc);
    vm->halted = true; vm->exit_code = code; vm_push(vm, value_null());
}
static void builtin_assert(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1) { ocl_free(args); vm_push(vm, value_null()); return; }
    bool ok = value_is_truthy(args[0]);
    if (!ok) {
        if (argc >= 2 && args[1].type==VALUE_STRING && args[1].data.string_val)
            fprintf(stderr, "ASSERTION FAILED: %s\n", args[1].data.string_val);
        else fprintf(stderr, "ASSERTION FAILED\n");
        FREE_ARGS(args, argc);
        vm->halted = true; vm->exit_code = 1; vm_push(vm, value_null()); return;
    }
    FREE_ARGS(args, argc); vm_push(vm, value_null());
}

static void builtin_is_null(VM *vm, int argc) {
    Value *a = pop_args(vm, argc); bool r = (argc < 1 || a[0].type == VALUE_NULL);
    FREE_ARGS(a, argc); vm_push(vm, value_bool(r));
}
static void builtin_is_int(VM *vm, int argc) {
    Value *a = pop_args(vm, argc); bool r = (argc >= 1 && a[0].type == VALUE_INT);
    FREE_ARGS(a, argc); vm_push(vm, value_bool(r));
}
static void builtin_is_float(VM *vm, int argc) {
    Value *a = pop_args(vm, argc); bool r = (argc >= 1 && a[0].type == VALUE_FLOAT);
    FREE_ARGS(a, argc); vm_push(vm, value_bool(r));
}
static void builtin_is_string(VM *vm, int argc) {
    Value *a = pop_args(vm, argc); bool r = (argc >= 1 && a[0].type == VALUE_STRING);
    FREE_ARGS(a, argc); vm_push(vm, value_bool(r));
}
static void builtin_is_bool(VM *vm, int argc) {
    Value *a = pop_args(vm, argc); bool r = (argc >= 1 && a[0].type == VALUE_BOOL);
    FREE_ARGS(a, argc); vm_push(vm, value_bool(r));
}

/* ── Array builtins ───────────────────────────────────────────────── */

/* arrayNew(size) → Array — creates an Array pre-filled with nulls */
static void builtin_array_new(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t sz = (argc >= 1) ? to_int64(args[0]) : 0;
    FREE_ARGS(args, argc);
    if (sz < 0) sz = 0;
    OclArray *arr = ocl_array_new((size_t)sz);
    for (int64_t i = 0; i < sz; i++) ocl_array_push(arr, value_null());
    vm_push(vm, value_array(arr));
    /* value_array() retains arr — no extra release needed */
}

/* arrayPush(arr, value) → null — appends value to arr */
static void builtin_array_push(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2 || args[0].type != VALUE_ARRAY) {
        FREE_ARGS(args, argc);
        vm_push(vm, value_null()); return;
    }
    ocl_array_push(args[0].data.array_val, args[1]);
    FREE_ARGS(args, argc);
    vm_push(vm, value_null());
}

/* arrayPop(arr) → value — removes and returns last element */
static void builtin_array_pop(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 1 || args[0].type != VALUE_ARRAY || !args[0].data.array_val || args[0].data.array_val->length == 0) {
        FREE_ARGS(args, argc);
        vm_push(vm, value_null()); return;
    }
    OclArray *arr = args[0].data.array_val;
    size_t last = arr->length - 1;
    Value v = value_own_copy(arr->elements[last]);
    value_free(arr->elements[last]);
    arr->elements[last] = value_null();
    arr->length--;
    FREE_ARGS(args, argc);
    vm_push(vm, v);
}

/* arrayGet(arr, idx) → value */
static void builtin_array_get(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 2 || args[0].type != VALUE_ARRAY) {
        FREE_ARGS(args, argc);
        vm_push(vm, value_null()); return;
    }
    int64_t idx = to_int64(args[1]);
    Value v = (idx >= 0 && args[0].data.array_val && (size_t)idx < args[0].data.array_val->length)
              ? ocl_array_get(args[0].data.array_val, (size_t)idx) : value_null();
    FREE_ARGS(args, argc);
    vm_push(vm, v);
}

/* arraySet(arr, idx, value) → null */
static void builtin_array_set(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc < 3 || args[0].type != VALUE_ARRAY) {
        FREE_ARGS(args, argc);
        vm_push(vm, value_null()); return;
    }
    int64_t idx = to_int64(args[1]);
    if (idx >= 0 && args[0].data.array_val) ocl_array_set(args[0].data.array_val, (size_t)idx, args[2]);
    FREE_ARGS(args, argc);
    vm_push(vm, value_null());
}

/* arrayLen(arr) → int */
static void builtin_array_len(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t len = 0;
    if (argc >= 1 && args[0].type == VALUE_ARRAY && args[0].data.array_val)
        len = (int64_t)args[0].data.array_val->length;
    FREE_ARGS(args, argc);
    vm_push(vm, value_int(len));
}

/* ── Dispatch table ───────────────────────────────────────────────── */

static const StdlibEntry STDLIB_TABLE[] = {
    { BUILTIN_INPUT,       "input",       builtin_input       },
    { BUILTIN_READLINE,    "readLine",    builtin_readline    },
    { BUILTIN_ABS,         "abs",         builtin_abs         },
    { BUILTIN_SQRT,        "sqrt",        builtin_sqrt        },
    { BUILTIN_POW,         "pow",         builtin_pow         },
    { BUILTIN_SIN,         "sin",         builtin_sin         },
    { BUILTIN_COS,         "cos",         builtin_cos         },
    { BUILTIN_TAN,         "tan",         builtin_tan         },
    { BUILTIN_FLOOR,       "floor",       builtin_floor       },
    { BUILTIN_CEIL,        "ceil",        builtin_ceil        },
    { BUILTIN_ROUND,       "round",       builtin_round       },
    { BUILTIN_MAX,         "max",         builtin_max         },
    { BUILTIN_MIN,         "min",         builtin_min         },
    { BUILTIN_STRLEN,      "strLen",      builtin_strlen      },
    { BUILTIN_SUBSTR,      "substr",      builtin_substr      },
    { BUILTIN_TOUPPER,     "toUpperCase", builtin_toupper     },
    { BUILTIN_TOLOWER,     "toLowerCase", builtin_tolower     },
    { BUILTIN_STRCONTAINS, "strContains", builtin_strcontains },
    { BUILTIN_STRINDEXOF,  "strIndexOf",  builtin_strindexof  },
    { BUILTIN_STRREPLACE,  "strReplace",  builtin_strreplace  },
    { BUILTIN_STRTRIM,     "strTrim",     builtin_strtrim     },
    { BUILTIN_STRSPLIT,    "strSplit",    builtin_strsplit    },
    { BUILTIN_TO_INT,      "toInt",       builtin_to_int      },
    { BUILTIN_TO_FLOAT,    "toFloat",     builtin_to_float    },
    { BUILTIN_TO_STRING,   "toString",    builtin_to_string   },
    { BUILTIN_TO_BOOL,     "toBool",      builtin_to_bool     },
    { BUILTIN_TYPEOF,      "typeOf",      builtin_typeof      },
    { BUILTIN_EXIT,        "exit",        builtin_exit        },
    { BUILTIN_ASSERT,      "assert",      builtin_assert      },
    { BUILTIN_IS_NULL,     "isNull",      builtin_is_null     },
    { BUILTIN_IS_INT,      "isInt",       builtin_is_int      },
    { BUILTIN_IS_FLOAT,    "isFloat",     builtin_is_float    },
    { BUILTIN_IS_STRING,   "isString",    builtin_is_string   },
    { BUILTIN_IS_BOOL,     "isBool",      builtin_is_bool     },
    /* Array builtins */
    { BUILTIN_ARRAY_NEW,   "arrayNew",    builtin_array_new   },
    { BUILTIN_ARRAY_PUSH,  "arrayPush",   builtin_array_push  },
    { BUILTIN_ARRAY_POP,   "arrayPop",    builtin_array_pop   },
    { BUILTIN_ARRAY_GET,   "arrayGet",    builtin_array_get   },
    { BUILTIN_ARRAY_SET,   "arraySet",    builtin_array_set   },
    { BUILTIN_ARRAY_LEN,   "arrayLen",    builtin_array_len   },
};
static const size_t STDLIB_TABLE_SIZE = sizeof(STDLIB_TABLE)/sizeof(STDLIB_TABLE[0]);

void stdlib_init(void) {}
void stdlib_cleanup(void) {}

bool stdlib_dispatch(VM *vm, int id, int argc) {
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++)
        if (STDLIB_TABLE[i].id == id) { STDLIB_TABLE[i].fn(vm, argc); return true; }
    return false;
}
const StdlibEntry *stdlib_lookup_by_name(const char *name) {
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++)
        if (strcmp(STDLIB_TABLE[i].name, name) == 0) return &STDLIB_TABLE[i];
    return NULL;
}
const StdlibEntry *stdlib_get_table(size_t *out_count) {
    if (out_count) *out_count = STDLIB_TABLE_SIZE;
    return STDLIB_TABLE;
}