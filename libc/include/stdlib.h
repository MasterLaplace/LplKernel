#ifndef _STDLIB_H
#define _STDLIB_H

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long size_t;

__attribute__((__noreturn__)) void abort(void);

#ifdef __cplusplus
}
#endif

#endif
