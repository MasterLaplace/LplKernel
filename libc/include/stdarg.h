/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
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
