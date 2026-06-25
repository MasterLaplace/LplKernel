#ifndef _STDLIB_H
#define _STDLIB_H

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __SIZE_TYPE__ size_t;

__attribute__((__noreturn__)) void abort(void);

/* Declarations only. The kernel allocates via kmalloc/kfree, not malloc/free —
 * these exist so toolchain headers that name them (notably <mm_malloc.h>, pulled
 * by <immintrin.h> for SSE intrinsics) resolve at name lookup. The inline
 * _mm_malloc/_mm_free are never odr-used in the kernel, so no definition is
 * required; an actual call would surface as a link error, the intended guard. */
void *malloc(size_t size);
void free(void *pointer);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
