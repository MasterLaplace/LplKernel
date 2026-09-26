/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file stdlib.h
 * @brief General utilities of the kernel C library.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-05-31
 **************************************************************************/

#ifndef _STDLIB_H
#define _STDLIB_H

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __SIZE_TYPE__ size_t;

__attribute__((__noreturn__)) void abort(void);

/**
 * @name Heap allocation, declared and never defined
 *
 * The kernel allocates via kmalloc/kfree, not malloc/free. These exist so toolchain
 * headers that name them (notably <mm_malloc.h>, pulled by <immintrin.h> for SSE
 * intrinsics) resolve at name lookup. The inline _mm_malloc/_mm_free are never odr-used
 * in the kernel, so no definition is required; an actual call would surface as a link
 * error, the intended guard.
 * @{
 */
void *malloc(size_t size);
void free(void *pointer);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
/** @} */

#ifdef __cplusplus
}
#endif

#endif
