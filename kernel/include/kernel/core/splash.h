/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
 * @file splash.h
 * @brief Boot splash screen and its loading bar.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-31
 **************************************************************************/

#ifndef KERNEL_CORE_SPLASH_H
#define KERNEL_CORE_SPLASH_H

#include <stdint.h>

/**
 * @brief Initializes the splash screen loading bar state.
 * @param total_steps The absolute total number of segments for the progress bar.
 */
extern void kernel_splash_initialize(uint32_t total_steps);

/**
 * @brief Updates the splash screen with a new loading step name.
 * @param step_name The short description of the initialization piece.
 */
extern void kernel_splash_update(const char *step_name);

/**
 * @brief Finishes the loading sequence, completely clears the terminal
 *        and resets the cursor for the final kernel welcome message.
 */
extern void kernel_splash_finish(void);

#endif /* KERNEL_CORE_SPLASH_H */
