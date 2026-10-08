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
 * @file cpu_identity.h
 * @brief What the processor says it is, and what it runs under, read from CPUID.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-08
 **************************************************************************/

#ifndef KERNEL_CPU_CPU_IDENTITY_H
#define KERNEL_CPU_CPU_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Characters of a CPUID signature: three registers of four bytes, and a terminator. */
#define CPU_IDENTITY_SIGNATURE_SIZE 13u

/**
 * @struct CpuIdentity_t
 * @brief The processor's vendor, family, model and stepping, and the hypervisor it runs under.
 */
typedef struct {
    char vendor[CPU_IDENTITY_SIGNATURE_SIZE]; /**< Leaf 0's vendor string, as "GenuineIntel". */
    uint32_t family;                          /**< Leaf 1's family, the extended one added. */
    uint32_t model;                           /**< Leaf 1's model, the extended one added where it counts. */
    uint32_t stepping;                        /**< Leaf 1's stepping. */
    bool hypervisor;                          /**< Leaf 1's ECX bit 31: a hypervisor says it is there. */
    char hypervisor_signature[CPU_IDENTITY_SIGNATURE_SIZE]; /**< Leaf 0x40000000's signature, empty without one. */
} CpuIdentity_t;

/**
 * @brief Reads the processor's identity from CPUID.
 *
 * @details The family and model are the displayed ones: the extended family is added when the base
 *          family is 15, and the extended model is prefixed when the family is 6 or 15. The
 *          hypervisor signature is read only when leaf 1 sets the hypervisor bit, and its trailing
 *          NULs are dropped ("KVMKVMKVM").
 *
 * @param out Receives the identity.
 */
void cpu_identity_read(CpuIdentity_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CPU_CPU_IDENTITY_H */
