/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
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
