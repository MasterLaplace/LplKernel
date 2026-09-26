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
 * @file stdatomic.h
 * @brief Atomic operations of the kernel C library, over the GCC builtins.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-09-26
 **************************************************************************/

#ifndef _STDATOMIC_H
#define _STDATOMIC_H

#include <stdint.h>
#include <stdbool.h>

#define __ATOMIC_RELAXED 0
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

/**
 * @brief Loads a value from an atomic variable with acquire semantics.
 *
 * @param p Pointer to the atomic variable.
 * @return The value loaded from the atomic variable.
 */
static inline uint32_t atomic_load_acquire(volatile uint32_t *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

/**
 * @brief Stores a value to an atomic variable with release semantics.
 *
 * @param p Pointer to the atomic variable.
 * @param v The value to store.
 */
static inline void atomic_store_release(volatile uint32_t *p, uint32_t v)
{
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/**
 * @brief Atomically adds a value to an atomic variable and returns the previous value.
 *
 * @param p Pointer to the atomic variable.
 * @param v The value to add.
 * @return The previous value of the atomic variable.
 */
static inline uint32_t atomic_fetch_add(volatile uint32_t *p, uint32_t v)
{
    return __atomic_fetch_add(p, v, __ATOMIC_ACQ_REL);
}

/**
 * @brief Atomically subtracts a value from an atomic variable and returns the previous value.
 *
 * @param p Pointer to the atomic variable.
 * @param v The value to subtract.
 * @return The previous value of the atomic variable.
 */
static inline uint32_t atomic_fetch_sub(volatile uint32_t *p, uint32_t v)
{
    return __atomic_fetch_sub(p, v, __ATOMIC_ACQ_REL);
}

/**
 * @brief Atomically performs an exchange operation on an atomic variable.
 *
 * @param p Pointer to the atomic variable.
 * @param v The value to exchange with the atomic variable.
 * @return The previous value of the atomic variable.
 */
static inline uint32_t atomic_exchange(volatile uint32_t *p, uint32_t v)
{
    return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL);
}

/**
 * @brief Atomically compares the value of an atomic variable with an expected value and, if they are equal, replaces it with a desired value.
 *
 * @param p Pointer to the atomic variable.
 * @param expected Pointer to the expected value. If the comparison fails, this will be updated with the actual value of the atomic variable.
 * @param desired The value to store if the comparison succeeds.
 * @return true if the exchange was successful (the values were equal), false otherwise.
 */
static inline bool atomic_compare_exchange(volatile uint32_t *p, uint32_t *expected, uint32_t desired)
{
    return __atomic_compare_exchange_n(p, expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#endif /* !STDATOMIC_H */
