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
 * @file console.h
 * @brief Interactive kernel console, over the keyboard and COM1.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-31
 **************************************************************************/

#ifndef KERNEL_CORE_CONSOLE_H
#define KERNEL_CORE_CONSOLE_H

#include <kernel/drivers/serial.h>

/**
 * @brief Whether this image carries an interactive command surface.
 *
 * A shell is the one facility an immutable, API-only node is defined by NOT
 * having: no arbitrary command means no configuration drift between two boots of
 * the same image, and nothing interactive to reach. The console is a development
 * convenience, so it is compiled in while developing (`KERNEL_CONSOLE=1`, the
 * default of both build paths) and compiled out of a production image
 * (`xmake -m release`).
 */
#if defined(LPL_KERNEL_ENABLE_CONSOLE)
#    define KERNEL_CONSOLE_IS_COMPILED_IN true
#else
#    define KERNEL_CONSOLE_IS_COMPILED_IN false
#endif

/**
 * @brief Report whether this image carries an interactive command surface.
 *
 * Emitted at boot rather than from the loop, because the loop only runs on the
 * profile that has no engine to run instead — so a report from inside it would be
 * missing from exactly the images anyone would want to check.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void kernel_console_report_surface(Serial_t *com1);

/**
 * @brief Enters the interactive kernel console loop.
 *
 * Starts a non-blocking loop handling keyboard and serial input
 * to process basic kernel diagnostic commands.
 *
 * The symbol exists in both configurations, so callers need no conditional. When
 * the surface is compiled out, the function reports its absence and idles rather
 * than returning: returning would fall out of kernel_main into the destructor,
 * which prints a panic line for what is a normal end of boot.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void kernel_console_run_interactive_loop(Serial_t *com1);

#endif /* KERNEL_CORE_CONSOLE_H */
