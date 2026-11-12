/*
 * OCL freestanding stdio streams.
 *
 * FILE is a thin, unbuffered wrapper over the port layer's descriptors.
 * No host stdio is used.
 */
#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OclFile {
    int fd;
    int err;
    int eof;
    int readable;
    int writable;
    int owns_fd;
};

static struct OclFile g_stdin  = {0, 0, 0, 1, 0, 0};
static struct OclFile g_stdout = {1, 0, 0, 0, 1, 0};
static struct OclFile g_stderr = {2, 0, 0, 0, 1, 0};

FILE *stdin  = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) { errno = EINVAL; return NULL; }

    int flags = 0;
    int readable = 0, writable = 0;

    switch (mode[0]) {
        case 'r': flags = OCL_O_RDONLY; readable = 1; break;
        case 'w': flags = OCL_O_WRONLY | OCL_O_CREAT | OCL_O_TRUNC; writable = 1; break;
        case 'a': flags = OCL_O_WRONLY | OCL_O_CREAT | OCL_O_APPEND; writable = 1; break;
        default:  errno = EINVAL; return NULL;
    }
    for (const char *m = mode + 1; *m; m++) {
        if (*m == '+') { flags = (flags & ~3) | OCL_O_RDWR; readable = writable = 1; }
        else if (*m == 'b') { /* binary: no-op on POSIX */ }
    }

    long fd = ocl_sys_open(path, flags, 0644);
    if (fd < 0) { errno = (int)-fd; return NULL; }

    struct OclFile *f = (struct OclFile *)malloc(sizeof(*f));
    if (!f) { ocl_sys_close((int)fd); errno = ENOMEM; return NULL; }

    f->fd = (int)fd; f->err = 0; f->eof = 0;
    f->readable = readable; f->writable = writable; f->owns_fd = 1;
    return f;
}

int fclose(FILE *f) {
    if (!f) { errno = EBADF; return EOF; }
    int rc = 0;
    if (f->owns_fd) {
        long r = ocl_sys_close(f->fd);
        if (r < 0) { errno = (int)-r; rc = EOF; }
        free(f);
    }
    return rc;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || !ptr || size == 0 || nmemb == 0) return 0;
    if (!f->readable) { f->err = 1; errno = EBADF; return 0; }

    size_t want = size * nmemb;
    size_t got  = 0;
    unsigned char *dst = (unsigned char *)ptr;

    while (got < want) {
        long r = ocl_sys_read(f->fd, dst + got, want - got);
        if (r < 0) { errno = (int)-r; f->err = 1; break; }
        if (r == 0) { f->eof = 1; break; }
        got += (size_t)r;
    }
    return got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || !ptr || size == 0 || nmemb == 0) return 0;
    if (!f->writable) { f->err = 1; errno = EBADF; return 0; }

    size_t want = size * nmemb;
    size_t put  = 0;
    const unsigned char *src = (const unsigned char *)ptr;

    while (put < want) {
        long r = ocl_sys_write(f->fd, src + put, want - put);
        if (r <= 0) { errno = (r < 0) ? (int)-r : EIO; f->err = 1; break; }
        put += (size_t)r;
    }
    return put / size;
}

int fseek(FILE *f, long offset, int whence) {
    if (!f) { errno = EBADF; return -1; }
    long r = ocl_sys_lseek(f->fd, offset, whence);
    if (r < 0) { errno = (int)-r; f->err = 1; return -1; }
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    if (!f) { errno = EBADF; return -1; }
    long r = ocl_sys_lseek(f->fd, 0, SEEK_CUR);
    if (r < 0) { errno = (int)-r; f->err = 1; return -1; }
    return r;
}

void rewind(FILE *f) {
    if (!f) return;
    (void)fseek(f, 0, SEEK_SET);
    f->err = 0;
}

int ferror(FILE *f) { return f ? f->err : 1; }
int feof(FILE *f)   { return f ? f->eof : 0; }
int fflush(FILE *f) { (void)f; return 0; }

int fgetc(FILE *f) {
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return EOF;
    return (int)c;
}

char *fgets(char *s, int n, FILE *f) {
    if (!s || n <= 0 || !f) return NULL;

    int i = 0;
    while (i < n - 1) {
        unsigned char c;
        if (fread(&c, 1, 1, f) != 1) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *f) {
    unsigned char b = (unsigned char)c;
    if (fwrite(&b, 1, 1, f) != 1) return EOF;
    return b;
}

int fputs(const char *s, FILE *f) {
    if (!s) return EOF;
    size_t n = strlen(s);
    return (fwrite(s, 1, n, f) == n) ? 0 : EOF;
}

int putchar(int c) { return fputc(c, stdout); }

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return (fputc('\n', stdout) == EOF) ? EOF : 0;
}

void perror(const char *s) {
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}
