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
 * @file ring3.h
 * @brief Entering ring 3, for the user-mode smoke test.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-19
 **************************************************************************/

#ifndef RING3_H_
#define RING3_H_

#include <stdint.h>

extern void ring3_enter(uint32_t user_eip, uint32_t user_esp);

#endif /* !RING3_H_ */
