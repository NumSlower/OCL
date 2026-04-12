#ifndef OCL_NUMOS_STDLIB_H
#define OCL_NUMOS_STDLIB_H

#include "libc.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
long long strtoll(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
char *getenv(const char *name);
void abort(void) __attribute__((noreturn));
int rand(void);
void srand(unsigned int seed);

#endif
