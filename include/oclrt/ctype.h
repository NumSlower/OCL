/*
 * OCL runtime ctype.h — implemented in src/libc/misc.c.
 */
#ifndef OCL_FS_CTYPE_H
#define OCL_FS_CTYPE_H

int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int toupper(int c);
int tolower(int c);

#endif /* OCL_FS_CTYPE_H */
