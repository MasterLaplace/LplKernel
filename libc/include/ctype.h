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
 * @file ctype.h
 * @brief Character classification of the kernel C library.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

#ifndef _CTYPE_H
#define _CTYPE_H

#define _bounds(c, lo, hi) (((c) >= (lo)) && (c <= (hi)))
#define _lower(c)          ((c) | (1 << 5))
#define _upper(c)          ((c) & ~(1 << 5))

/**
 * @name Character classification
 *
 * See https://en.cppreference.com/w/c/string/byte/isprint for the ASCII ranges.
 */

#define iscntrl(c) _bounds(c, '\0', ' ')

#define isprint(c) _bounds(c, ' ', '~')

#define isspace(c) (_bounds(c, '\t', '\r') || ((c) == ' ') || ((c) == 127))

#define isblank(c) ((c) == '\t' || ((c) == ' '))

#define isgraph(c) (isprint(c) && ((c) != ' '))

#define ispunc(c) (_bounds(c, '!', '/') || _bounds(c, ':', '@') || _bounds(c, '[', '`'))

#define isalpha(c) _bounds(_lower(c), 'a', 'z')

#define isupper(c) _bounds(c, 'A', 'Z')

#define islower(c) _bounds(c, 'a', 'z')

#define isdigit(c) _bounds(c, '0', '9')

#define isxdigit(c) (isdigit(c) || _bounds(_lower(c), 'a', 'f'))

#define tolower(c) (isupper(c) ? (_lower(c)) : (c))
#define toupper(c) (islower(c) ? (_upper(c)) : (c))

#endif
