#include "common.h"
#include <math.h>
#include <inttypes.h>
#include <time.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define TokenType WindowsTokenType
#  include <windows.h>
#  undef TokenType
#endif

#if defined(_WIN32)
#  define strcasecmp _stricmp
#else
#  include <strings.h>
#endif

#if defined(DEBUG)
typedef struct {
    void   *ptr;
    size_t  size;
} MemEntry;

#define MT_TOMBSTONE ((void *)(intptr_t)-1)

static MemEntry *g_mt       = NULL;
static size_t    g_mt_cap   = 0;
static size_t    g_mt_cnt   = 0;
static size_t    g_mt_bytes = 0;
static int       g_mt_on    = -1; /* -1 unknown, 0 off, 1 on */

static bool mt_env_enabled(void) {
    const char *s = getenv("OCL_MEMTRACE");
    if (!s) return false;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '\0') return false;
    if (!strcmp(s, "0") || !strcasecmp(s, "false") || !strcasecmp(s, "no") || !strcasecmp(s, "off"))
        return false;
    return true;
}

bool ocl_memtrace_enabled(void) {
    if (g_mt_on == -1) g_mt_on = mt_env_enabled() ? 1 : 0;
    return g_mt_on == 1;
}

static size_t mt_hash(void *p) {
    uintptr_t x = (uintptr_t)p;
    x >>= 4;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x;
}

static void mt_init(void) {
    if (g_mt) return;
    g_mt_cap = 1024;
    g_mt = (MemEntry *)malloc(g_mt_cap * sizeof(MemEntry));
    if (!g_mt) { fputs("ocl: fatal: out of memory\n", stderr); abort(); }
    for (size_t i = 0; i < g_mt_cap; i++) g_mt[i] = (MemEntry){0};
}

static void mt_rehash(size_t new_cap) {
    MemEntry *old = g_mt;
    size_t old_cap = g_mt_cap;

    MemEntry *nt = (MemEntry *)malloc(new_cap * sizeof(MemEntry));
    if (!nt) { fputs("ocl: fatal: out of memory\n", stderr); abort(); }
    for (size_t i = 0; i < new_cap; i++) nt[i] = (MemEntry){0};

    g_mt = nt;
    g_mt_cap = new_cap;
    g_mt_cnt = 0;
    g_mt_bytes = 0;

    for (size_t i = 0; i < old_cap; i++) {
        void *p = old[i].ptr;
        if (!p || p == MT_TOMBSTONE) continue;

        size_t mask = g_mt_cap - 1;
        size_t idx = mt_hash(p) & mask;
        while (g_mt[idx].ptr) idx = (idx + 1) & mask;
        g_mt[idx] = old[i];
        g_mt_cnt++;
        g_mt_bytes += old[i].size;
    }

    free(old);
}

static void mt_maybe_grow(void) {
    if (g_mt_cap == 0) mt_init();
    if ((g_mt_cnt + 1) * 10 < g_mt_cap * 7) return;
    mt_rehash(g_mt_cap ? g_mt_cap * 2 : 1024);
}

static void mt_put(void *ptr, size_t size) {
    if (!ptr) return;
    mt_maybe_grow();
    size_t mask = g_mt_cap - 1;
    size_t idx = mt_hash(ptr) & mask;
    size_t first_tomb = (size_t)-1;

    while (g_mt[idx].ptr) {
        if (g_mt[idx].ptr == ptr) {
            if (g_mt[idx].size != size) {
                if (g_mt[idx].size < size) g_mt_bytes += (size - g_mt[idx].size);
                else g_mt_bytes -= (g_mt[idx].size - size);
                g_mt[idx].size = size;
            }
            return;
        }
        if (g_mt[idx].ptr == MT_TOMBSTONE && first_tomb == (size_t)-1)
            first_tomb = idx;
        idx = (idx + 1) & mask;
    }

    if (first_tomb != (size_t)-1) idx = first_tomb;
    g_mt[idx].ptr = ptr;
    g_mt[idx].size = size;
    g_mt_cnt++;
    g_mt_bytes += size;
}

static void mt_del(void *ptr) {
    if (!ptr || !g_mt_cap || !g_mt) return;
    size_t mask = g_mt_cap - 1;
    size_t idx = mt_hash(ptr) & mask;
    while (g_mt[idx].ptr) {
        if (g_mt[idx].ptr == ptr) {
            g_mt_bytes -= g_mt[idx].size;
            g_mt[idx].ptr = MT_TOMBSTONE;
            g_mt[idx].size = 0;
            g_mt_cnt--;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

size_t ocl_memtrace_leak_count(size_t *out_bytes) {
    if (out_bytes) *out_bytes = 0;
    if (!ocl_memtrace_enabled()) return 0;
    if (!g_mt) return 0;
    if (out_bytes) *out_bytes = g_mt_bytes;
    return g_mt_cnt;
}

size_t ocl_memtrace_snprint(char *buf, size_t bufsize, size_t max_entries) {
    if (!buf || bufsize == 0) return 0;
    buf[0] = '\0';
    if (!ocl_memtrace_enabled() || !g_mt || g_mt_cnt == 0) return 0;

    size_t written = 0;
    size_t shown = 0;
    for (size_t i = 0; i < g_mt_cap && shown < max_entries; i++) {
        void *p = g_mt[i].ptr;
        if (!p || p == MT_TOMBSTONE) continue;

        int n = snprintf(buf + written, bufsize - written,
                         "%s%p(%llu)",
                         (shown == 0 ? "" : ", "),
                         p, (unsigned long long)g_mt[i].size);
        if (n < 0) break;
        if ((size_t)n >= bufsize - written) break;
        written += (size_t)n;
        shown++;
    }
    return written;
}

void ocl_memtrace_shutdown(void) {
    if (!g_mt) return;
    free(g_mt);
    g_mt = NULL;
    g_mt_cap = g_mt_cnt = g_mt_bytes = 0;
}
#else
bool ocl_memtrace_enabled(void) { return false; }
size_t ocl_memtrace_leak_count(size_t *out_bytes) { if (out_bytes) *out_bytes = 0; return 0; }
size_t ocl_memtrace_snprint(char *buf, size_t bufsize, size_t max_entries) {
    (void)max_entries;
    if (buf && bufsize) buf[0] = '\0';
    return 0;
}
void ocl_memtrace_shutdown(void) { }
#endif

/* ── Memory helpers ───────────────────────────────────────────────── */

void *ocl_malloc(size_t size) {
    if (size == 0) return NULL;
    void *p = malloc(size);
    if (!p) {
        fputs("ocl: fatal: out of memory\n", stderr);
        abort();
    }
#if defined(DEBUG)
    if (ocl_memtrace_enabled()) mt_put(p, size);
#endif
    return p;
}

void *ocl_realloc(void *ptr, size_t size) {
    if (size == 0) {
#if defined(DEBUG)
        if (ocl_memtrace_enabled()) mt_del(ptr);
#endif
        free(ptr);
        return NULL;
    }
#if defined(DEBUG)
    bool track = ocl_memtrace_enabled();
    if (track && ptr) mt_del(ptr);
#endif
    void *p = realloc(ptr, size);
    if (!p) {
        fputs("ocl: fatal: out of memory (realloc)\n", stderr);
        abort();
    }
#if defined(DEBUG)
    if (track) mt_put(p, size);
#endif
    return p;
}

void ocl_free(void *ptr) {
#if defined(DEBUG)
    if (ocl_memtrace_enabled()) mt_del(ptr);
#endif
    free(ptr);
}

char *ocl_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char  *d   = ocl_malloc(len + 1);
    memcpy(d, s, len + 1);
    return d;
}

/* ── Array heap object ────────────────────────────────────────────── */

OclArray *ocl_array_new(size_t initial_cap) {
    OclArray *a  = ocl_malloc(sizeof(OclArray));
    a->capacity  = initial_cap > 0 ? initial_cap : 8;
    a->elements  = ocl_malloc(a->capacity * sizeof(Value));
    a->length    = 0;
    a->refcount  = 1;   /* creator holds the initial reference */

    for (size_t i = 0; i < a->capacity; i++)
        a->elements[i] = value_null();

    return a;
}

void ocl_array_retain(OclArray *a) {
    if (a) a->refcount++;
}

void ocl_array_release(OclArray *a) {
    if (!a) return;
    if (--a->refcount > 0) return;

    for (size_t i = 0; i < a->length; i++)
        value_free(a->elements[i]);
    ocl_free(a->elements);
    ocl_free(a);
}

static void array_ensure_capacity(OclArray *a, size_t idx) {
    if (idx < a->capacity) return;

    size_t new_cap = a->capacity * 2;
    if (new_cap <= idx) new_cap = idx + 8;

    a->elements = ocl_realloc(a->elements, new_cap * sizeof(Value));
    for (size_t i = a->capacity; i < new_cap; i++)
        a->elements[i] = value_null();
    a->capacity = new_cap;
}

void ocl_array_set(OclArray *a, size_t idx, Value v) {
    if (!a) return;
    array_ensure_capacity(a, idx);
    value_free(a->elements[idx]);
    a->elements[idx] = value_own_copy(v);
    if (idx >= a->length) a->length = idx + 1;
}

Value ocl_array_get(OclArray *a, size_t idx) {
    if (!a || idx >= a->length) return value_null();

    Value v = a->elements[idx];
    if (v.type == VALUE_STRING)
        return value_string_borrow(v.data.string_val);
    if (v.type == VALUE_ARRAY)
        ocl_array_retain(v.data.array_val);
    return v;
}

void ocl_array_push(OclArray *a, Value v) {
    if (!a) return;
    ocl_array_set(a, a->length, v);
}

static int struct_field_index(const OclStruct *s, const char *name) {
    if (!s || !name) return -1;
    for (size_t i = 0; i < s->field_count; i++) {
        if (s->field_names[i] && strcmp(s->field_names[i], name) == 0)
            return (int)i;
    }
    return -1;
}

OclStruct *ocl_struct_new(const char *type_name, size_t field_count) {
    OclStruct *s = ocl_malloc(sizeof(OclStruct));
    s->type_name = ocl_strdup(type_name ? type_name : "Struct");
    s->field_count = field_count;
    s->refcount = 1;
    s->field_names = field_count > 0 ? ocl_malloc(field_count * sizeof(char *)) : NULL;
    s->field_values = field_count > 0 ? ocl_malloc(field_count * sizeof(Value)) : NULL;

    for (size_t i = 0; i < field_count; i++) {
        s->field_names[i] = NULL;
        s->field_values[i] = value_null();
    }
    return s;
}

void ocl_struct_retain(OclStruct *s) {
    if (s) s->refcount++;
}

void ocl_struct_release(OclStruct *s) {
    if (!s) return;
    if (--s->refcount > 0) return;

    for (size_t i = 0; i < s->field_count; i++) {
        ocl_free(s->field_names[i]);
        value_free(s->field_values[i]);
    }
    ocl_free(s->field_names);
    ocl_free(s->field_values);
    ocl_free(s->type_name);
    ocl_free(s);
}

bool ocl_struct_set(OclStruct *s, const char *name, Value v) {
    if (!s || !name) return false;

    int idx = struct_field_index(s, name);
    if (idx >= 0) {
        value_free(s->field_values[idx]);
        s->field_values[idx] = value_own_copy(v);
        return true;
    }

    for (size_t i = 0; i < s->field_count; i++) {
        if (!s->field_names[i]) {
            s->field_names[i] = ocl_strdup(name);
            s->field_values[i] = value_own_copy(v);
            return true;
        }
    }

    return false;
}

Value ocl_struct_get(OclStruct *s, const char *name, bool *found) {
    int idx = struct_field_index(s, name);
    if (idx < 0) {
        if (found) *found = false;
        return value_null();
    }
    if (found) *found = true;
    return value_own_copy(s->field_values[idx]);
}

OclCell *ocl_cell_new(Value value) {
    OclCell *cell = ocl_malloc(sizeof(OclCell));
    cell->value = value_own_copy(value);
    cell->refcount = 1;
    return cell;
}

void ocl_cell_retain(OclCell *cell) {
    if (cell) cell->refcount++;
}

void ocl_cell_release(OclCell *cell) {
    if (!cell) return;
    if (--cell->refcount > 0) return;
    value_free(cell->value);
    ocl_free(cell);
}

OclFunction *ocl_function_new(uint32_t function_index, size_t capture_count) {
    OclFunction *fn = ocl_malloc(sizeof(OclFunction));
    fn->function_index = function_index;
    fn->capture_count = capture_count;
    fn->captures = capture_count > 0 ? ocl_malloc(capture_count * sizeof(OclCell *)) : NULL;
    fn->refcount = 1;
    for (size_t i = 0; i < capture_count; i++)
        fn->captures[i] = NULL;
    return fn;
}

void ocl_function_retain(OclFunction *fn) {
    if (fn) fn->refcount++;
}

void ocl_function_release(OclFunction *fn) {
    if (!fn) return;
    if (--fn->refcount > 0) return;
    for (size_t i = 0; i < fn->capture_count; i++)
        ocl_cell_release(fn->captures[i]);
    ocl_free(fn->captures);
    ocl_free(fn);
}

/* ── String constructors ──────────────────────────────────────────── */

Value value_string_copy(const char *s) {
    return value_string(ocl_strdup(s ? s : ""));
}

Value value_own_copy(Value v) {
    if (v.type == VALUE_STRING)
        return value_string_copy(v.data.string_val);
    if (v.type == VALUE_ARRAY && v.data.array_val)
        ocl_array_retain(v.data.array_val);
    if (v.type == VALUE_STRUCT && v.data.struct_val)
        ocl_struct_retain(v.data.struct_val);
    if (v.type == VALUE_FUNCTION && v.data.function_val)
        ocl_function_retain(v.data.function_val);
    return v;
}

/* ── Truthiness ───────────────────────────────────────────────────── */

bool value_is_truthy(Value v) {
    switch (v.type) {
        case VALUE_BOOL:   return v.data.bool_val;
        case VALUE_INT:    return v.data.int_val != 0;
        case VALUE_FLOAT:  return v.data.float_val != 0.0;
        case VALUE_STRING: return v.data.string_val && v.data.string_val[0] != '\0';
        case VALUE_CHAR:   return v.data.char_val != '\0';
        case VALUE_ARRAY:  return v.data.array_val && v.data.array_val->length > 0;
        case VALUE_STRUCT: return v.data.struct_val != NULL;
        case VALUE_FUNCTION: return v.data.function_val != NULL;
        case VALUE_NULL:   return false;
    }
    return false;
}

/* ── Type name ────────────────────────────────────────────────────── */

const char *value_type_name(ValueType t) {
    switch (t) {
        case VALUE_INT:    return "Int";
        case VALUE_FLOAT:  return "Float";
        case VALUE_STRING: return "String";
        case VALUE_BOOL:   return "Bool";
        case VALUE_CHAR:   return "Char";
        case VALUE_ARRAY:  return "Array";
        case VALUE_STRUCT: return "Struct";
        case VALUE_FUNCTION: return "Function";
        case VALUE_NULL:   return "Null";
    }
    return "Unknown";
}

double ocl_monotonic_time_ms(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart != 0 && QueryPerformanceCounter(&counter))
        return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
    return (double)time(NULL) * 1000.0;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
    return (double)time(NULL) * 1000.0;
#else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC)
        return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
    return (double)time(NULL) * 1000.0;
#endif
}

/* ── Free ─────────────────────────────────────────────────────────── */

void value_free(Value v) {
    if (v.type == VALUE_STRING && v.owned && v.data.string_val)
        ocl_free(v.data.string_val);
    else if (v.type == VALUE_ARRAY && v.owned && v.data.array_val)
        ocl_array_release(v.data.array_val);
    else if (v.type == VALUE_STRUCT && v.owned && v.data.struct_val)
        ocl_struct_release(v.data.struct_val);
    else if (v.type == VALUE_FUNCTION && v.owned && v.data.function_val)
        ocl_function_release(v.data.function_val);
}

/* ── value_to_string — rotating static buffer pool ───────────────────
 *
 * Up to OCL_VTOS_POOL_COUNT nested calls (e.g. arrays of arrays) are safe;
 * beyond that, earlier buffers are silently reused.  Callers must not free
 * the returned pointer; strdup() it if longer lifetime is needed.
 */
char *value_to_string(Value v) {
    static char pool[OCL_VTOS_POOL_COUNT][OCL_VTOS_BUF_SIZE];
    static int  slot = 0;

    char *out  = pool[slot];
    slot       = (slot + 1) % OCL_VTOS_POOL_COUNT;

    switch (v.type) {
        case VALUE_INT:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%" PRId64, v.data.int_val);
            break;

        case VALUE_FLOAT:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%g", v.data.float_val);
            break;

        case VALUE_STRING:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%s",
                     v.data.string_val ? v.data.string_val : "");
            break;

        case VALUE_BOOL:
            snprintf(out, OCL_VTOS_BUF_SIZE, "%s",
                     v.data.bool_val ? "true" : "false");
            break;

        case VALUE_CHAR:
            out[0] = v.data.char_val;
            out[1] = '\0';
            break;

        case VALUE_NULL:
            memcpy(out, "null", 5);
            break;

        case VALUE_ARRAY: {
            OclArray *a = v.data.array_val;
            if (!a) { memcpy(out, "[]", 3); break; }

            size_t pos = 0;
            out[pos++] = '[';

            for (size_t i = 0; i < a->length; i++) {
                if (i > 0) {
                    if (pos + 2 < OCL_VTOS_BUF_SIZE - 1) {
                        out[pos++] = ',';
                        out[pos++] = ' ';
                    }
                }
                /* Recursive call consumes the *next* pool slot — safe up to depth 8. */
                const char *elem = value_to_string(a->elements[i]);
                size_t      elen = strlen(elem);
                if (pos + elen < OCL_VTOS_BUF_SIZE - 2) {
                    memcpy(out + pos, elem, elen);
                    pos += elen;
                }
            }

            if (pos < OCL_VTOS_BUF_SIZE - 1) out[pos++] = ']';
            out[pos] = '\0';
            break;
        }
        case VALUE_STRUCT: {
            OclStruct *s = v.data.struct_val;
            if (!s) {
                memcpy(out, "Struct {}", 10);
                break;
            }

            size_t pos = 0;
            const char *type_name = s->type_name ? s->type_name : "Struct";
            size_t type_len = strlen(type_name);
            if (type_len >= OCL_VTOS_BUF_SIZE - 4) type_len = OCL_VTOS_BUF_SIZE - 5;
            memcpy(out + pos, type_name, type_len);
            pos += type_len;
            if (pos + 3 < OCL_VTOS_BUF_SIZE) {
                out[pos++] = ' ';
                out[pos++] = '{';
            }

            bool first = true;
            for (size_t i = 0; i < s->field_count; i++) {
                if (!s->field_names[i]) continue;
                if (!first && pos + 2 < OCL_VTOS_BUF_SIZE - 1) {
                    out[pos++] = ',';
                    out[pos++] = ' ';
                }
                first = false;

                size_t name_len = strlen(s->field_names[i]);
                if (pos + name_len + 2 >= OCL_VTOS_BUF_SIZE - 1) break;
                memcpy(out + pos, s->field_names[i], name_len);
                pos += name_len;
                out[pos++] = ':';
                out[pos++] = ' ';

                const char *value_str = value_to_string(s->field_values[i]);
                size_t value_len = strlen(value_str);
                if (pos + value_len >= OCL_VTOS_BUF_SIZE - 2)
                    value_len = OCL_VTOS_BUF_SIZE - pos - 2;
                memcpy(out + pos, value_str, value_len);
                pos += value_len;
            }

            if (pos < OCL_VTOS_BUF_SIZE - 1) out[pos++] = '}';
            out[pos] = '\0';
            break;
        }

        case VALUE_FUNCTION:
            snprintf(out, OCL_VTOS_BUF_SIZE, "<function@%u>",
                     v.data.function_val ? v.data.function_val->function_index : 0u);
            break;
    }

    return out;
}
