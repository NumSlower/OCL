/*
 * OCL runtime stdio.h
 *
 * Included as <oclrt/stdio.h>; there is no host stdio in this project.
 * Everything here is implemented from scratch in src/libc/.
 */
#ifndef OCL_FS_STDIO_H
#define OCL_FS_STDIO_H

#include <oclrt/stddef.h>
#include <oclrt/stdarg.h>

typedef struct OclFile FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF      (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 1024

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int    fseek(FILE *f, long offset, int whence);
long   ftell(FILE *f);
void   rewind(FILE *f);
int    ferror(FILE *f);
int    feof(FILE *f);
int    fflush(FILE *f);
int    fgetc(FILE *f);
char  *fgets(char *s, int n, FILE *f);
int    fputc(int c, FILE *f);
int    fputs(const char *s, FILE *f);
int    putchar(int c);
int    puts(const char *s);

int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, size_t n, const char *fmt, ...);
int    vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);

void   perror(const char *s);

#endif /* OCL_FS_STDIO_H */
