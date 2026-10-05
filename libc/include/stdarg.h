/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file stdarg.h
 * @brief Variable argument lists of the kernel C library.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define _va_align(type) (((sizeof(type) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))

#define va_start(ap, last_fixed_arg) ((ap) = (va_list) & (last_fixed_arg) + _va_align(typeof(last_fixed_arg)))

#define va_arg(ap, type) (*(type *) (((ap) += _va_align(type)) - _va_align(type)))

#define va_end(ap) ((ap) = (va_list) 0)

#endif
