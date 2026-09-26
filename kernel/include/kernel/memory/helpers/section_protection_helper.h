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
 * @file section_protection_helper.h
 * @brief Telemetry record of the section protection pass.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-08-08
 **************************************************************************/

#ifndef KERNEL_MEMORY_SECTION_PROTECTION_HELPER_H
#define KERNEL_MEMORY_SECTION_PROTECTION_HELPER_H

#include <kernel/drivers/serial.h>
#include <stdbool.h>

/**
 * @brief Writes the outcome of the section protection pass as one telemetry record.
 *
 * @note `write_protect` is reported next to `applied` and not folded into it, because they
 *       fail differently and only one of them is about this kernel: `applied` says the page
 *       table entries were cleared, `write_protect` says the processor will act on them. A
 *       pass with WP clear looks exactly like a pass that works, and enforces nothing.
 *
 * @param serial  Output port.
 * @param applied What kernel_section_protection_apply returned.
 */
extern void write_section_protection_info(Serial_t *serial, bool applied);

#endif /* KERNEL_MEMORY_SECTION_PROTECTION_HELPER_H */
