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
 * @file platform.h
 * @brief What the host harness gives the kernel's tests in place of a booted machine.
 *
 * A debug kernel's tests read a boot module, and the firmware of the machine it boots on. The host
 * harness has neither until it is given them: a module file, served as the bootloader would serve
 * it, and the tables a debug kernel printed, served as that machine's firmware. The tests then run
 * on the same bytes on both sides.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef TOOLS_ACPI_HOST_PLATFORM_H
#define TOOLS_ACPI_HOST_PLATFORM_H

#include <kernel/acpi/host.h>

#include <stdint.h>

/**
 * @brief Serves a boot module, which boot_module_find finds by the end of @p name.
 *
 * @param name   What a bootloader gives as its command line: the path it was loaded from.
 * @param bytes  Its bytes, kept by the caller as long as the tests run.
 * @param length How many there are.
 */
extern void platform_serve_boot_module(const char *name, const uint8_t *bytes, uint32_t length);

/**
 * @brief Makes @p firmware the firmware of the machine the tests boot on, which
 *        advanced_configuration_and_power_interface_machine_host reads through.
 *
 * @param firmware The host that serves it, kept by the caller as long as the tests run; NULL gives a
 *                 machine with no firmware again.
 */
extern void platform_serve_firmware(const AdvancedConfigurationAndPowerInterfaceHost_t *firmware);

#endif /* TOOLS_ACPI_HOST_PLATFORM_H */
