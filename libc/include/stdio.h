/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file stdio.h
 * @brief Standard input/output of the kernel C library.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-05-31
 **************************************************************************/

#ifndef _STDIO_H
#define _STDIO_H

#include <sys/cdefs.h>

#define SEEK_SET 0
#define EOF      (-1)
typedef struct {
    int unused;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE *stderr;
#define stderr stderr

int printf(const char *__restrict, ...);
int putchar(int);
int puts(const char *);

#ifdef __cplusplus
}
#endif

#endif
