/*
 * OCL freestanding stdlib.h — implemented in src/libc/alloc.c and misc.c.
 */
#ifndef OCL_FS_STDLIB_H
#define OCL_FS_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX     2147483647

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

void  abort(void) __attribute__((noreturn));
void  exit(int status) __attribute__((noreturn));

int    atoi(const char *s);
long   strtol(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);

int           abs(int v);
long          labs(long v);
long long     llabs(long long v);

int  rand(void);
void srand(unsigned int seed);

#endif /* OCL_FS_STDLIB_H */
