/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file stdckdint.h
 * @brief Checked integer arithmetic of the kernel C library, C23's <stdckdint.h> over the GCC builtins: each
 *        operation computes the exact result, stores it wrapped to the result's type, and says whether it fit.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-16
 **************************************************************************/

#ifndef _STDCKDINT_H
#define _STDCKDINT_H

#include <stdbool.h>

#define __STDC_VERSION_STDCKDINT_H__ 202311L /**< The C23 revision this header follows. */

/**
 * @brief Stores @p a + @p b in @p result, wrapped to its type.
 *
 * @param result Where the sum goes: a pointer to an integer type other than bool or an enumeration.
 * @param a      An integer, of any integer type.
 * @param b      An integer, of any integer type.
 * @return true when the exact sum does not fit @p result's type.
 */
#define ckd_add(result, a, b) __builtin_add_overflow((a), (b), (result))

/**
 * @brief Stores @p a - @p b in @p result, wrapped to its type.
 *
 * @param result Where the difference goes: a pointer to an integer type other than bool or an enumeration.
 * @param a      An integer, of any integer type.
 * @param b      An integer, of any integer type.
 * @return true when the exact difference does not fit @p result's type.
 */
#define ckd_sub(result, a, b) __builtin_sub_overflow((a), (b), (result))

/**
 * @brief Stores @p a * @p b in @p result, wrapped to its type.
 *
 * @param result Where the product goes: a pointer to an integer type other than bool or an enumeration.
 * @param a      An integer, of any integer type.
 * @param b      An integer, of any integer type.
 * @return true when the exact product does not fit @p result's type.
 */
#define ckd_mul(result, a, b) __builtin_mul_overflow((a), (b), (result))

#endif /* _STDCKDINT_H */
