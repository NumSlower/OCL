#ifndef OCL_NUMOS_STRING_H
#define OCL_NUMOS_STRING_H

#include "libc.h"

char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
char *strstr(const char *haystack, const char *needle);
char *strrchr(const char *s, int c);
char *strerror(int errnum);

#endif
