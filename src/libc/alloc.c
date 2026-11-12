/*
 * OCL freestanding allocator.
 *
 * A first-fit free-list allocator with boundary coalescing, backed by
 * anonymous memory obtained from the port layer. No libc malloc is used.
 */
#include "port.h"
#include <oclrt/stdint.h>
#include <oclrt/string.h>

#define OCL_ALIGN      16u
#define OCL_MIN_SPLIT  (sizeof(Block) + OCL_ALIGN)
#define OCL_CHUNK_MIN  (1u << 20)      /* 1 MiB */
#define OCL_BLOCK_MAGIC 0x0C1B10C4u

typedef struct Block {
    size_t        size;    /* payload bytes, header excluded */
    struct Block *next;    /* free-list link (valid only while free) */
    uint32_t      magic;
    uint32_t      free;
} Block;

#define HDR ((size_t)sizeof(Block))

static Block *g_free_list = NULL;

static size_t align_up(size_t n) {
    return (n + (OCL_ALIGN - 1)) & ~(size_t)(OCL_ALIGN - 1);
}

/* Insert a free block into the address-ordered free list and coalesce. */
static void free_list_insert(Block *b) {
    Block *prev = NULL;
    Block *cur  = g_free_list;

    while (cur && cur < b) { prev = cur; cur = cur->next; }

    b->next = cur;
    b->free = 1;
    if (prev) prev->next = b; else g_free_list = b;

    /* merge with successor */
    if (b->next && (char *)b + HDR + b->size == (char *)b->next) {
        Block *n = b->next;
        b->size += HDR + n->size;
        b->next  = n->next;
    }
    /* merge with predecessor */
    if (prev && (char *)prev + HDR + prev->size == (char *)b) {
        prev->size += HDR + b->size;
        prev->next  = b->next;
    }
}

static int grow(size_t need) {
    size_t len = need + HDR + OCL_ALIGN;
    if (len < OCL_CHUNK_MIN) len = OCL_CHUNK_MIN;
    len = (len + 4095u) & ~(size_t)4095u;

    void *mem = ocl_sys_map(len);
    if (!mem) { errno = ENOMEM; return 0; }

    Block *b = (Block *)mem;
    b->size  = len - HDR;
    b->next  = NULL;
    b->magic = OCL_BLOCK_MAGIC;
    b->free  = 1;
    free_list_insert(b);
    return 1;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = align_up(size);

    Block *prev = NULL;
    Block *b    = g_free_list;

    while (b) {
        if (b->size >= size) break;
        prev = b;
        b    = b->next;
    }

    if (!b) {
        if (!grow(size)) return NULL;
        prev = NULL;
        b    = g_free_list;
        while (b && b->size < size) { prev = b; b = b->next; }
        if (!b) return NULL;
    }

    /* unlink */
    if (prev) prev->next = b->next; else g_free_list = b->next;

    /* split when the remainder can hold a header plus one aligned unit */
    if (b->size >= size + OCL_MIN_SPLIT) {
        Block *rest = (Block *)((char *)b + HDR + size);
        rest->size  = b->size - size - HDR;
        rest->magic = OCL_BLOCK_MAGIC;
        rest->next  = NULL;
        rest->free  = 1;
        b->size     = size;
        free_list_insert(rest);
    }

    b->free  = 0;
    b->next  = NULL;
    b->magic = OCL_BLOCK_MAGIC;
    return (void *)((char *)b + HDR);
}

void free(void *ptr) {
    if (!ptr) return;
    Block *b = (Block *)((char *)ptr - HDR);
    if (b->magic != OCL_BLOCK_MAGIC || b->free) return;   /* ignore double free */
    free_list_insert(b);
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    size_t total = nmemb * size;
    if (size != 0 && total / size != nmemb) { errno = ENOMEM; return NULL; }
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    Block *b = (Block *)((char *)ptr - HDR);
    if (b->magic != OCL_BLOCK_MAGIC) return NULL;

    size_t want = align_up(size);
    if (b->size >= want) {
        if (b->size >= want + OCL_MIN_SPLIT) {
            Block *rest = (Block *)((char *)b + HDR + want);
            rest->size  = b->size - want - HDR;
            rest->magic = OCL_BLOCK_MAGIC;
            rest->next  = NULL;
            rest->free  = 1;
            b->size     = want;
            free_list_insert(rest);
        }
        return ptr;
    }

    void *fresh = malloc(size);
    if (!fresh) return NULL;
    memcpy(fresh, ptr, b->size);
    free(ptr);
    return fresh;
}

void abort(void) {
    static const char msg[] = "ocl: abort\n";
    ocl_sys_write(2, msg, sizeof(msg) - 1);
    ocl_sys_exit(134);
}

void exit(int status) {
    ocl_sys_exit(status);
}

int       abs(int v)       { return v < 0 ? -v : v; }
long      labs(long v)     { return v < 0 ? -v : v; }
long long llabs(long long v) { return v < 0 ? -v : v; }
