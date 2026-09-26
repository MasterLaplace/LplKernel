/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
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
