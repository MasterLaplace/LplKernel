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
 * @file identity.h
 * @brief What this kernel image says about itself at boot.
 *
 * The version comes from kernel/include/kernel/config.h, the build (profile and
 * mode) and the commit are stamped by the build into identity.c alone, so a new
 * commit recompiles one file. A boot log therefore says which kernel it is.
 *
 * @author @MasterLaplace
 * @version 0.0.5
 * @date 2026-10-05
 **************************************************************************/

#ifndef KERNEL_CORE_IDENTITY_H
#define KERNEL_CORE_IDENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The kernel's configuration, one KEY=value per line: the version with its build and
 *        commit, the system, the processor, the compiler and the debug flag.
 *
 * @return A string with static storage, ending in a newline.
 */
extern const char *kernel_identity_configuration(void);

/**
 * @brief The kernel's identity as one telemetry record, for instance
 *        `[LPLTLM] build version=0.0.5+server.debug commit=12ec228`.
 *
 * @return A string with static storage, ending in a newline.
 */
extern const char *kernel_identity_telemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* !KERNEL_CORE_IDENTITY_H */
