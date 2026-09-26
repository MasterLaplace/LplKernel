/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file stddef.h
 * @brief Standard definitions of the kernel C library.
 *
 * The fundamental types are deferred to the compiler so they match exactly what the
 * toolchain (and libstdc++'s <cstddef>) expects. On i686-elf gcc, size_t is
 * `long unsigned int` (4 bytes) — NOT `unsigned int`; hardcoding the latter created
 * a type clash the moment any C++ TU pulled in both this header and the compiler's
 * <cstddef> (e.g. via the engine module / kstd).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL (void *) (0UL)

typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;

#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

/**
 * @brief The most-aligned type, in the compiler's notion of it.
 *
 * @details <cstddef> exposes std::max_align_t via `using ::max_align_t;`, so the C header
 *          must define it.
 */
typedef struct {
    long long __max_align_ll;
    long double __max_align_ld;
} max_align_t;

#ifdef __GNUC__
#    define offsetof(type, member) __builtin_offsetof(type, member)
#else
#    define offsetof(type, member) ((size_t) & (((type *) 0)->member))
#endif
#endif
