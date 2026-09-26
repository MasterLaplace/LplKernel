/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
 * @file init_array.h
 * @brief C++ global constructor and destructor runtime (.init_array ABI).
 *
 * Two C++ static-initialization ABIs coexist in this tree:
 *
 *   - Legacy `.ctors` (emitted by i686-elf gcc 10.2.0): walked by `_init`
 *     (crti prologue + crtbegin's __do_global_ctors_aux + crtn epilogue), which
 *     boot.S calls directly. `.init_array` is empty in this case.
 *
 *   - Modern `.init_array` (emitted by newer gcc / clang, like the ARM port):
 *     `_init` does NOT walk it, so the kernel must iterate it explicitly. This
 *     is what keeps the boot path correct after the planned toolchain upgrade.
 *
 * Calling both `_init` (from boot.S) and kernel_run_global_constructors() is
 * safe: exactly one of `.ctors` / `.init_array` is populated for any given
 * compiler, so no constructor runs twice.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-25
 **************************************************************************/

#ifndef KERNEL_BOOT_INIT_ARRAY_H
#define KERNEL_BOOT_INIT_ARRAY_H

#include <stdint.h>

/**
 * @brief Runs every entry in `.init_array`, in registration order.
 *
 * @details No-op when the active toolchain emits constructors into the legacy `.ctors`
 *          section.
 */
void kernel_run_global_constructors(void);

/**
 * @brief Runs every entry in `.fini_array`, in reverse registration order.
 */
void kernel_run_global_destructors(void);

/**
 * @brief Boot-time self-test of the static-initialization machinery.
 *
 * @details A sentinel constructor records a magic value when the machinery fires.
 *
 * @return 1 once that constructor has run (after `_init` and/or
 *         kernel_run_global_constructors()), 0 otherwise.
 */
uint8_t kernel_constructor_self_test_passed(void);

#endif /* !KERNEL_BOOT_INIT_ARRAY_H */
