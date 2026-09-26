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
 * @file limits.h
 * @brief Integer limits of the kernel C library.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT   8
#define MB_LEN_MAX 1

#define SCHAR_MAX +127
#define SCHAR_MIN -127

#define CHAR_MAX  SCHAR_MAX
#define CHAR_MIN  SCHAR_MIN
#define UCHAR_MAX 255

#define SHRT_MAX  +32767
#define SHRT_MIN  -32767
#define USHRT_MAX 65535

#define INT_MAX  2147483647
#define INT_MIN  (-INT_MAX - 1)
#define UINT_MAX 4294967295U

#define LONG_MAX  +2147483647L
#define LONG_MIN  (-LONG_MAX - 1L)
#define ULONG_MAX 4294967295

#define LLONG_MAX  +9223372036854775807LL
#define LLONG_MIN  (-LLONG_MAX - 1LL)
#define ULLONG_MAX 18446744073709551615ULL

#endif
