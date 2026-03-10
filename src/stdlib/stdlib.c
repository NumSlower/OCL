#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <inttypes.h>

#include "common.h"
#include "ocl_stdlib.h"
#include "vm.h"

/* ══════════════════════════════════════════════════════════════════
   Argument helpers
   ══════════════════════════════════════════════════════════════════ */

static Value *pop_args(VM *vm, int argc) {
    if (argc <= 0) return NULL;
    Value *args = ocl_malloc((size_t)argc * sizeof(Value));
    for (int i = argc - 1; i >= 0; i--)
        args[i] = vm_pop(vm);
    return args;
}

static void free_args(Value *args, int argc) {
    if (!args) return;
    for (int i = 0; i < argc; i++) value_free(args[i]);
    ocl_free(args);
}

#define REQUIRE_ARGS(vm, args, argc, needed, name) \
    do { \
        if ((argc) < (needed)) { \
            fprintf(stderr, "ocl: %s requires %d argument(s), got %d\n", \
                    (name), (needed), (argc)); \
            free_args((args), (argc)); \
            vm_push((vm), value_null()); \
            return; \
        } \
    } while (0)

static int64_t to_int64(Value v) {
    switch (v.type) {
        case VALUE_INT:    return v.data.int_val;
        case VALUE_FLOAT:  return (int64_t)v.data.float_val;
        case VALUE_BOOL:   return v.data.bool_val ? 1 : 0;
        case VALUE_CHAR:   return (int64_t)(unsigned char)v.data.char_val;
        case VALUE_STRING: return v.data.string_val
                                  ? (int64_t)strtoll(v.data.string_val, NULL, 10)
                                  : 0;
        default: return 0;
    }
}

/* ══════════════════════════════════════════════════════════════════
   I/O
   ══════════════════════════════════════════════════════════════════ */

static void builtin_input(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);

    if (argc >= 1 && args[0].type == VALUE_STRING && args[0].data.string_val)
        fputs(args[0].data.string_val, stdout);
    fflush(stdout);
    free_args(args, argc);

    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) {
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

    vm_push(vm, value_string(ocl_strdup(buf)));
}

static void builtin_readline(VM *vm, int argc) {
    builtin_input(vm, argc);
}

/* ══════════════════════════════════════════════════════════════════
   String functions
   ══════════════════════════════════════════════════════════════════ */

static void builtin_strlen(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "strLen");
    int64_t len = (args[0].type == VALUE_STRING && args[0].data.string_val)
                  ? (int64_t)strlen(args[0].data.string_val) : 0;
    free_args(args, argc);
    vm_push(vm, value_int(len));
}

static void builtin_substr(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "substr");

    if (args[0].type != VALUE_STRING) {
        free_args(args, argc);
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    const char *src  = args[0].data.string_val ? args[0].data.string_val : "";
    size_t      slen = strlen(src);
    int64_t     start  = to_int64(args[1]);
    int64_t     length = (argc >= 3) ? to_int64(args[2]) : (int64_t)slen;
    free_args(args, argc);

    if (start  < 0)      start  = 0;
    if (length < 0)      length = 0;
    if ((size_t)start >= slen) { vm_push(vm, value_string(ocl_strdup(""))); return; }
    if ((size_t)(start + length) > slen) length = (int64_t)(slen - (size_t)start);

    char *result = ocl_malloc((size_t)length + 1);
    memcpy(result, src + start, (size_t)length);
    result[length] = '\0';
    vm_push(vm, value_string(result));
}

static void builtin_toupper(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toUpperCase");
    char *result = ocl_strdup(args[0].type == VALUE_STRING && args[0].data.string_val
                              ? args[0].data.string_val : "");
    free_args(args, argc);
    for (char *p = result; *p; p++) *p = (char)toupper((unsigned char)*p);
    vm_push(vm, value_string(result));
}

static void builtin_tolower(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "toLowerCase");
    char *result = ocl_strdup(args[0].type == VALUE_STRING && args[0].data.string_val
                              ? args[0].data.string_val : "");
    free_args(args, argc);
    for (char *p = result; *p; p++) *p = (char)tolower((unsigned char)*p);
    vm_push(vm, value_string(result));
}

static void builtin_strcontains(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "strContains");
    const char *hay    = (args[0].type == VALUE_STRING) ? args[0].data.string_val : "";
    const char *needle = (args[1].type == VALUE_STRING) ? args[1].data.string_val : "";
    bool found = hay && needle && (strstr(hay, needle) != NULL);
    free_args(args, argc);
    vm_push(vm, value_bool(found));
}

static void builtin_strindexof(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "strIndexOf");
    const char *hay    = (args[0].type == VALUE_STRING) ? args[0].data.string_val : "";
    const char *needle = (args[1].type == VALUE_STRING) ? args[1].data.string_val : "";
    int64_t idx = -1;
    if (hay && needle) {
        const char *pos = strstr(hay, needle);
        if (pos) idx = (int64_t)(pos - hay);
    }
    free_args(args, argc);
    vm_push(vm, value_int(idx));
}

static void builtin_strreplace(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 3, "strReplace");

    if (args[0].type != VALUE_STRING) {
        free_args(args, argc);
        vm_push(vm, value_string(ocl_strdup("")));
        return;
    }
    const char *src     = args[0].data.string_val ? args[0].data.string_val : "";
    const char *old_str = (args[1].type == VALUE_STRING) ? args[1].data.string_val : "";
    const char *new_str = (args[2].type == VALUE_STRING) ? args[2].data.string_val : "";

    if (!old_str || old_str[0] == '\0') {
        char *copy = ocl_strdup(src);
        free_args(args, argc);
        vm_push(vm, value_string(copy));
        return;
    }

    size_t old_len = strlen(old_str);
    size_t new_len = new_str ? strlen(new_str) : 0;
    size_t src_len = strlen(src);

    size_t      count = 0;
    const char *cur   = src;
    while ((cur = strstr(cur, old_str)) != NULL) { count++; cur += old_len; }

    size_t  result_len = src_len + count * (new_len > old_len ? new_len - old_len : old_len - new_len) + 1;
    char   *result     = ocl_malloc(result_len);
    char   *dest       = result;
    cur = src;

    const char *found;
    while ((found = strstr(cur, old_str)) != NULL) {
        size_t chunk = (size_t)(found - cur);
        memcpy(dest, cur, chunk);
        dest += chunk;
        if (new_str) { memcpy(dest, new_str, new_len); dest += new_len; }
        cur = found + old_len;
    }
    strcpy(dest, cur);

    free_args(args, argc);
    vm_push(vm, value_string(result));
}

static void builtin_strtrim(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "strTrim");
    const char *s = (args[0].type == VALUE_STRING && args[0].data.string_val)
                    ? args[0].data.string_val : "";
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char *result = ocl_malloc(len + 1);
    memcpy(result, s, len);
    result[len] = '\0';
    free_args(args, argc);
    vm_push(vm, value_string(result));
}

static void builtin_strsplit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "strSplit");
    const char *str   = (args[0].type == VALUE_STRING && args[0].data.string_val)
                        ? args[0].data.string_val : "";
    const char *delim = (args[1].type == VALUE_STRING && args[1].data.string_val)
                        ? args[1].data.string_val : "";
    free_args(args, argc);
    if (!delim || delim[0] == '\0') {
        vm_push(vm, value_int((int64_t)strlen(str)));
        return;
    }
    int64_t count = 0;
    size_t  dlen  = strlen(delim);
    const char *cur = str, *found_ptr;
    while ((found_ptr = strstr(cur, delim)) != NULL) { count++; cur = found_ptr + dlen; }
    if (str[0] != '\0') count++;
    vm_push(vm, value_int(count));
}

/* ══════════════════════════════════════════════════════════════════
   Utilities
   ══════════════════════════════════════════════════════════════════ */

static void builtin_exit(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int code = (argc >= 1) ? (int)to_int64(args[0]) : 0;
    free_args(args, argc);
    vm->halted    = true;
    vm->exit_code = code;
    vm_push(vm, value_null());
}

/* ══════════════════════════════════════════════════════════════════
   Array built-ins
   ══════════════════════════════════════════════════════════════════ */

static void builtin_array_new(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    int64_t sz  = (argc >= 1) ? to_int64(args[0]) : 0;
    free_args(args, argc);

    if (sz < 0) sz = 0;
    OclArray *arr = ocl_array_new((size_t)sz);
    for (int64_t i = 0; i < sz; i++)
        ocl_array_push(arr, value_null());
    vm_push(vm, value_array(arr));
    ocl_array_release(arr);
}

static void builtin_array_push(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "arrayPush");

    if (args[0].type != VALUE_ARRAY) {
        fprintf(stderr, "ocl: arrayPush: first argument must be Array\n");
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    ocl_array_push(args[0].data.array_val, args[1]);
    free_args(args, argc);
    vm_push(vm, value_null());
}

static void builtin_array_pop(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "arrayPop");

    if (args[0].type != VALUE_ARRAY || !args[0].data.array_val ||
        args[0].data.array_val->length == 0) {
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    OclArray *arr  = args[0].data.array_val;
    size_t    last = arr->length - 1;
    Value     v    = value_own_copy(arr->elements[last]);
    value_free(arr->elements[last]);
    arr->elements[last] = value_null();
    arr->length--;
    free_args(args, argc);
    vm_push(vm, v);
}

static void builtin_array_get(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 2, "arrayGet");

    if (args[0].type != VALUE_ARRAY) {
        fprintf(stderr, "ocl: arrayGet: first argument must be Array\n");
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    int64_t idx = to_int64(args[1]);
    Value   v   = (idx >= 0 && args[0].data.array_val &&
                   (size_t)idx < args[0].data.array_val->length)
                  ? ocl_array_get(args[0].data.array_val, (size_t)idx)
                  : value_null();
    free_args(args, argc);
    vm_push(vm, v);
}

static void builtin_array_set(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 3, "arraySet");

    if (args[0].type != VALUE_ARRAY) {
        fprintf(stderr, "ocl: arraySet: first argument must be Array\n");
        free_args(args, argc);
        vm_push(vm, value_null());
        return;
    }
    int64_t idx = to_int64(args[1]);
    if (idx >= 0 && args[0].data.array_val)
        ocl_array_set(args[0].data.array_val, (size_t)idx, args[2]);
    else if (idx < 0)
        fprintf(stderr, "ocl: arraySet: negative index %"PRId64"\n", idx);
    free_args(args, argc);
    vm_push(vm, value_null());
}

static void builtin_array_len(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    REQUIRE_ARGS(vm, args, argc, 1, "arrayLen");
    int64_t len = (args[0].type == VALUE_ARRAY && args[0].data.array_val)
                  ? (int64_t)args[0].data.array_val->length : 0;
    free_args(args, argc);
    vm_push(vm, value_int(len));
}

/* ══════════════════════════════════════════════════════════════════
   Random
   ══════════════════════════════════════════════════════════════════ */

static uint32_t ocl_rand32(void) {
    static bool seeded = false;
    if (!seeded) {
        unsigned int seed;
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) { fread(&seed, sizeof(seed), 1, f); fclose(f); }
        else     seed = (unsigned int)time(NULL);
        srand(seed);
        seeded = true;
    }
    return (uint32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
}

static void builtin_random(VM *vm, int argc) {
    Value *args = pop_args(vm, argc);
    if (argc >= 2) {
        int64_t  lo    = to_int64(args[0]);
        int64_t  hi    = to_int64(args[1]);
        free_args(args, argc);
        if (hi <= lo) { vm_push(vm, value_int(lo)); return; }
        uint64_t range = (uint64_t)(hi - lo) + 1;
        vm_push(vm, value_int(lo + (int64_t)(ocl_rand32() % range)));
    } else if (argc == 1) {
        int64_t n = to_int64(args[0]);
        free_args(args, argc);
        if (n <= 0) { vm_push(vm, value_int(0)); return; }
        vm_push(vm, value_int((int64_t)(ocl_rand32() % (uint64_t)n)));
    } else {
        free_args(args, argc);
        vm_push(vm, value_float((double)ocl_rand32() / ((double)UINT32_MAX + 1.0)));
    }
}

/* ══════════════════════════════════════════════════════════════════
   Dispatch table
   ══════════════════════════════════════════════════════════════════ */

static const StdlibEntry STDLIB_TABLE[] = {
    { BUILTIN_INPUT,       "input",       builtin_input       },
    { BUILTIN_READLINE,    "readLine",    builtin_readline    },
    { BUILTIN_STRLEN,      "strLen",      builtin_strlen      },
    { BUILTIN_SUBSTR,      "substr",      builtin_substr      },
    { BUILTIN_TOUPPER,     "toUpperCase", builtin_toupper     },
    { BUILTIN_TOLOWER,     "toLowerCase", builtin_tolower     },
    { BUILTIN_STRCONTAINS, "strContains", builtin_strcontains },
    { BUILTIN_STRINDEXOF,  "strIndexOf",  builtin_strindexof  },
    { BUILTIN_STRREPLACE,  "strReplace",  builtin_strreplace  },
    { BUILTIN_STRTRIM,     "strTrim",     builtin_strtrim     },
    { BUILTIN_STRSPLIT,    "strSplit",    builtin_strsplit    },
    { BUILTIN_EXIT,        "exit",        builtin_exit        },
    { BUILTIN_ARRAY_NEW,   "arrayNew",    builtin_array_new   },
    { BUILTIN_ARRAY_PUSH,  "arrayPush",   builtin_array_push  },
    { BUILTIN_ARRAY_POP,   "arrayPop",    builtin_array_pop   },
    { BUILTIN_ARRAY_GET,   "arrayGet",    builtin_array_get   },
    { BUILTIN_ARRAY_SET,   "arraySet",    builtin_array_set   },
    { BUILTIN_ARRAY_LEN,   "arrayLen",    builtin_array_len   },
    { BUILTIN_RANDOM,      "random",      builtin_random      },
};

static const size_t STDLIB_TABLE_SIZE =
    sizeof(STDLIB_TABLE) / sizeof(STDLIB_TABLE[0]);

/* ── Public API ───────────────────────────────────────────────────── */

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
    if (!name) return NULL;
    for (size_t i = 0; i < STDLIB_TABLE_SIZE; i++)
        if (strcmp(STDLIB_TABLE[i].name, name) == 0)
            return &STDLIB_TABLE[i];
    return NULL;
}

const StdlibEntry *stdlib_get_table(size_t *out_count) {
    if (out_count) *out_count = STDLIB_TABLE_SIZE;
    return STDLIB_TABLE;
}