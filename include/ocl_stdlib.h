#ifndef OCL_STDLIB_H
#define OCL_STDLIB_H
#include "ast.h"
#include "common.h"
struct VM; typedef struct VM VM;
#define OCL_VARIADIC_ARGS(min_args) (-(int)((min_args) + 1))
#define OCL_ARGS_VARIADIC(param_count) ((param_count) < 0)
#define OCL_ARGS_MIN(param_count) ((param_count) < 0 ? (-(param_count) - 1) : (param_count))
#define BUILTIN_PRINT       1
#define BUILTIN_PRINTF      2
#define BUILTIN_INPUT       3
#define BUILTIN_READLINE    4
#define BUILTIN_READFILE    5
#define BUILTIN_WRITEFILE   6
#define BUILTIN_APPENDFILE  7
#define BUILTIN_ABS         10
#define BUILTIN_SQRT        11
#define BUILTIN_POW         12
#define BUILTIN_SIN         13
#define BUILTIN_COS         14
#define BUILTIN_TAN         15
#define BUILTIN_FLOOR       16
#define BUILTIN_CEIL        17
#define BUILTIN_ROUND       18
#define BUILTIN_MAX         19
#define BUILTIN_MIN         20
#define BUILTIN_STRLEN      30
#define BUILTIN_SUBSTR      31
#define BUILTIN_TOUPPER     32
#define BUILTIN_TOLOWER     33
#define BUILTIN_STRCONTAINS 34
#define BUILTIN_STRINDEXOF  35
#define BUILTIN_STRREPLACE  36
#define BUILTIN_STRTRIM     37
#define BUILTIN_STRSPLIT    38
#define BUILTIN_TOINT       40
#define BUILTIN_TOFLOAT     41
#define BUILTIN_TOSTRING    42
#define BUILTIN_STRFORMAT   53
#define BUILTIN_TOBOOL      43
#define BUILTIN_TYPEOF      44
#define BUILTIN_ISNULL      45
#define BUILTIN_ISINT       46
#define BUILTIN_ISFLOAT     47
#define BUILTIN_ISSTRING    48
#define BUILTIN_ISBOOL      49
#define BUILTIN_EXIT        50
#define BUILTIN_ASSERT      51
#define BUILTIN_TIMENOW     52
/* Array builtins */
#define BUILTIN_ARRAY_NEW   60
#define BUILTIN_ARRAY_PUSH  61
#define BUILTIN_ARRAY_POP   62
#define BUILTIN_ARRAY_GET   63
#define BUILTIN_ARRAY_SET   64
#define BUILTIN_ARRAY_LEN   65
#define BUILTIN_RANDOM      67
#define BUILTIN_LISTFILES   68
#define BUILTIN_MEASUREFILE 69
#define BUILTIN_LOGICAL_SHIFT_RIGHT 70
#define BUILTIN_ROTATE_LEFT 71
#define BUILTIN_ROTATE_RIGHT 72
#define BUILTIN_POPCOUNT    73
#define BUILTIN_CLZ         74
#define BUILTIN_CTZ         75
#define BUILTIN_BIT_TEST    76
#define BUILTIN_BIT_SET     77
#define BUILTIN_BIT_CLEAR   78
#define BUILTIN_BIT_TOGGLE  79
#define BUILTIN_BIT_NAND    80
#define BUILTIN_BIT_NOR     81
#define BUILTIN_BIT_XNOR    82
#define BUILTIN_TERMINAL_ARGS          90
#define BUILTIN_TERMINAL_EXEC          91
#define BUILTIN_TERMINAL_CAPTURE       92
#define BUILTIN_TERMINAL_SHELL         93
#define BUILTIN_TERMINAL_SHELL_CAPTURE 94
#define BUILTIN_TERMINAL_OS            95
#define BUILTIN_FFI_READ_U8            96
#define BUILTIN_FFI_READ_U16           97
#define BUILTIN_FFI_READ_U32           98
#define BUILTIN_FFI_READ_U64           99
#define BUILTIN_FFI_READ_I8            100
#define BUILTIN_FFI_READ_I16           101
#define BUILTIN_FFI_READ_I32           102
#define BUILTIN_FFI_READ_I64           103
#define BUILTIN_FFI_READ_F32           104
#define BUILTIN_FFI_READ_F64           105
#define BUILTIN_FFI_READ_PTR           106
#define BUILTIN_FFI_WRITE_U8           107
#define BUILTIN_FFI_WRITE_U16          108
#define BUILTIN_FFI_WRITE_U32          109
#define BUILTIN_FFI_WRITE_U64          110
#define BUILTIN_FFI_WRITE_I8           111
#define BUILTIN_FFI_WRITE_I16          112
#define BUILTIN_FFI_WRITE_I32          113
#define BUILTIN_FFI_WRITE_I64          114
#define BUILTIN_FFI_WRITE_F32          115
#define BUILTIN_FFI_WRITE_F64          116
#define BUILTIN_FFI_WRITE_PTR          117
#define BUILTIN_FFI_CSTRING            118

typedef struct {
    int id;
    const char *name;
    void (*fn)(VM *vm, int argc);
    int param_count;
    BuiltinType return_type;
} StdlibEntry;
bool               stdlib_dispatch(VM *vm, int id, int argc);
const StdlibEntry *stdlib_lookup_by_name(const char *name);
const StdlibEntry *stdlib_get_table(size_t *out_count);
#endif
