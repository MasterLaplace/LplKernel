/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file stdbool.h
 * @brief Boolean type of the kernel C library.
 *
 * In C++, `bool`, `true` and `false` are keywords, and defining them as macros
 * breaks every header that uses them as identifiers — <type_traits> declares
 * `template<typename T, T __v>` over `bool`, so a macro turns the whole standard
 * library into syntax errors. The C standard says as much: in C++, <stdbool.h>
 * defines nothing but the feature-test macro.
 *
 * This mattered the moment a C header meant for both languages appeared. The
 * kernel's own headers are included from C++ by libengine and libassistant, and
 * without this guard a single `#include <stdbool.h>` three levels down poisons
 * the translation unit with several hundred errors that name only libstdc++
 * files.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

#ifndef _STDBOOL_H
#define _STDBOOL_H

#ifndef __cplusplus

#    define bool _Bool

#    define true  (_Bool) 1
#    define false (_Bool) 0

#endif /* !__cplusplus */

/** Feature-test macro, C99 §7.16 "Boolean type and values". */
#define __bool_true_false_are_defined 1
#endif
