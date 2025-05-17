#ifndef _STDIO_H
#define _STDIO_H

#include <sys/cdefs.h>

#define SEEK_SET 0
#define EOF (-1)
typedef struct { int unused; } FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE *stderr;
#define stderr stderr

int printf(const char * __restrict, ...);
int putchar(int);
int puts(const char *);

#ifdef __cplusplus
}
#endif

#endif
